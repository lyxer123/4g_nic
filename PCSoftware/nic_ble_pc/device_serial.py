"""PC ↔ device REST API over UART using firmware PCAPI bridge (loopback HTTP)."""

from __future__ import annotations

import json
import queue
import re
import threading
import time
from typing import Any, Callable, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError as e:  # pragma: no cover
    serial = None  # type: ignore
    list_ports = None  # type: ignore
    _IMPORT_ERR = e
else:
    _IMPORT_ERR = None

# 允许行首前有 ESP 日志等杂讯；固件单帧写出 "PCAPI_OUT <st> <len>\r\n" + body
_PCAPI_OUT_RE = re.compile(r"PCAPI_OUT\s+(\d+)\s+(\d+)\s*$")
_RE_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class SerialApiError(Exception):
    def __init__(self, message: str, *, status: Optional[int] = None, body: Optional[str] = None) -> None:
        super().__init__(message)
        self.status = status
        self.body = body


def list_com_ports() -> list[tuple[str, str]]:
    if list_ports is None:
        return []
    out: list[tuple[str, str]] = []
    for p in list_ports.comports():
        out.append((p.device, p.description or ""))
    return sorted(out, key=lambda x: x[0])


class SerialMux:
    """
    Single reader thread: demuxes PCAPI_OUT responses vs device log lines.
    """

    def __init__(
        self,
        *,
        log_line: Callable[[str], None],
        default_timeout_s: float = 120.0,
        log_pcapi: bool = True,
    ) -> None:
        if serial is None:
            raise RuntimeError("pyserial not installed") from _IMPORT_ERR
        self._log_line = log_line
        self._default_timeout_s = default_timeout_s
        self._log_pcapi = log_pcapi
        self._ser: Optional[serial.Serial] = None
        self._send_lock = threading.Lock()
        self._pending: Optional[queue.Queue[tuple[int, bytes]]] = None
        self._reader_t: Optional[threading.Thread] = None
        self._stop = threading.Event()

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def open(self, port: str, baud: int = 115200) -> None:
        self.close()
        self._stop.clear()
        self._ser = serial.Serial(port, baud, timeout=0.05)
        self._reader_t = threading.Thread(target=self._reader_loop, name="serial_mux", daemon=True)
        self._reader_t.start()

    def close(self) -> None:
        self._stop.set()
        if self._ser is not None:
            try:
                if self._ser.is_open:
                    self._ser.close()
            except Exception:
                pass
            self._ser = None
        if self._reader_t is not None:
            self._reader_t.join(timeout=2.0)
            self._reader_t = None
        self._pending = None

    def _emit_log(self, line: str) -> None:
        s = line.rstrip("\r\n")
        if not s:
            return
        self._log_line(s)

    def _log_pcapi_cmd(self, line: str, post_body: Optional[bytes]) -> None:
        if not self._log_pcapi:
            return
        self._log_line(f"[PCAPI →] {line.rstrip()}")
        if post_body and len(post_body) > 0:
            preview = post_body.decode("utf-8", errors="replace").replace("\r", " ").replace("\n", " ")
            max_preview = 800
            if len(preview) > max_preview:
                preview = preview[:max_preview] + "…"
            self._log_line(f"[PCAPI →] POST 载荷 {len(post_body)} 字节: {preview}")

    def _log_pcapi_result(self, status: int, data: bytes) -> None:
        if not self._log_pcapi:
            return
        self._log_line(f"[PCAPI ←] HTTP {status} · {len(data)} 字节")
        if not data:
            return
        text = data.decode("utf-8", errors="replace")
        one_line = " ".join(text.split())
        max_body = 4000
        if len(one_line) > max_body:
            one_line = one_line[:max_body] + " …(已截断)"
        self._log_line(f"[PCAPI ←] {one_line}")

    @staticmethod
    def _norm_line(s: str) -> str:
        s = _RE_ANSI.sub("", s)
        return s.replace("\r\n", "\n").replace("\r", "\n").strip()

    def _read_line(self) -> Optional[str]:
        if self._ser is None:
            return None
        raw = self._ser.readline()
        if not raw:
            return None
        try:
            return raw.decode("utf-8", errors="replace")
        except Exception:
            return repr(raw)

    def _read_exact(self, n: int, deadline: float) -> bytes:
        if self._ser is None or n <= 0:
            return b""
        buf = bytearray()
        while len(buf) < n and time.monotonic() < deadline:
            need = n - len(buf)
            chunk = self._ser.read(need)
            if chunk:
                buf.extend(chunk)
            else:
                time.sleep(0.02)
        if len(buf) != n:
            raise SerialApiError(f"short read: got {len(buf)} expected {n}")
        return bytes(buf)

    def _reader_loop(self) -> None:
        while not self._stop.is_set() and self._ser is not None:
            try:
                line = self._read_line()
                if line is None:
                    continue
                norm = self._norm_line(line)
                m = _PCAPI_OUT_RE.search(norm)
                if not m:
                    self._emit_log(line)
                    continue
                status = int(m.group(1))
                blen = int(m.group(2))
                body = self._read_exact(blen, time.monotonic() + self._default_timeout_s)
                tgt = self._pending
                if tgt is not None:
                    try:
                        tgt.put_nowait((status, body))
                    except queue.Full:
                        self._emit_log(line.strip() + f" [dropped body {blen}b]")
                else:
                    self._emit_log(f"[orphan PCAPI_OUT {status} {blen}b]")
            except Exception as e:
                if not self._stop.is_set():
                    self._emit_log(f"[serial_mux] {e}")

    def request(
        self,
        method: str,
        path: str,
        body: Optional[bytes] = None,
        *,
        timeout_s: Optional[float] = None,
    ) -> tuple[int, bytes]:
        if not self.is_open or self._ser is None:
            raise SerialApiError("串口未连接")
        if not path.startswith("/"):
            path = "/" + path
        method = method.upper().strip()
        tmo = timeout_s if timeout_s is not None else self._default_timeout_s

        line: str
        if method == "POST":
            bl = len(body) if body else 0
            line = f"PCAPI POST {path} {bl}\r\n"
        elif method in ("GET", "DELETE"):
            line = f"PCAPI {method} {path}\r\n"
        else:
            raise SerialApiError(f"unsupported method {method}")

        post_payload = body if method == "POST" and body else None
        self._log_pcapi_cmd(line, post_payload)

        resp_q: queue.Queue[tuple[int, bytes]] = queue.Queue(maxsize=2)
        with self._send_lock:
            self._pending = resp_q
            try:
                self._ser.write(line.encode("utf-8"))
                if method == "POST" and body and len(body) > 0:
                    self._ser.write(body)
                self._ser.flush()
                try:
                    st, data = resp_q.get(timeout=tmo)
                except queue.Empty as e:
                    if self._log_pcapi:
                        self._log_line(f"[PCAPI ←] 超时（{tmo:g}s 内未收到 PCAPI_OUT）")
                    raise SerialApiError("设备响应超时（PCAPI_OUT）") from e
            finally:
                self._pending = None

        self._log_pcapi_result(st, data)
        return st, data

    def send_raw_line(self, line: str) -> None:
        """
        Send one line to the device (e.g. serial_cli: help, modem_info, AT...).
        Uses the same lock as PCAPI to avoid interleaved writes. Response lines
        appear in the mux log as usual.
        """
        if not self.is_open or self._ser is None:
            raise SerialApiError("串口未连接")
        s = line.strip()
        if not s:
            raise SerialApiError("命令为空")
        payload = (s + "\r\n").encode("utf-8")
        with self._send_lock:
            self._ser.write(payload)
            self._ser.flush()
        self._log_line(f"[发送 →] {s}")


class SerialApiClient:
    """Same call pattern as WebApiClient: get / post_json / delete → parsed JSON."""

    def __init__(self, mux: SerialMux, *, timeout_s: float = 120.0) -> None:
        self._mux = mux
        self.timeout_s = timeout_s

    def _request(
        self,
        method: str,
        path: str,
        *,
        json_obj: Optional[dict[str, Any]] = None,
    ) -> Any:
        body: Optional[bytes] = None
        if json_obj is not None:
            body = json.dumps(json_obj, ensure_ascii=False).encode("utf-8")
        status, raw = self._mux.request(method, path, body, timeout_s=self.timeout_s)
        text = raw.decode("utf-8", errors="replace")
        if status < 200 or status >= 300:
            msg = text
            try:
                j = json.loads(text) if text else {}
                msg = str(j.get("message") or j.get("status") or text or f"HTTP {status}")
            except json.JSONDecodeError:
                pass
            raise SerialApiError(msg, status=status, body=text)
        if not text.strip():
            return None
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"raw": text}

    def get(self, path: str) -> Any:
        return self._request("GET", path)

    def post_json(self, path: str, obj: dict[str, Any]) -> Any:
        return self._request("POST", path, json_obj=obj)

    def delete(self, path: str) -> Any:
        return self._request("DELETE", path)
