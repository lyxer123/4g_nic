"""Blocking serial reader thread → line callback (COM / idf monitor style)."""

from __future__ import annotations

import threading
import time
from typing import Callable, List, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError as e:  # pragma: no cover
    serial = None  # type: ignore
    list_ports = None  # type: ignore
    _IMPORT_ERR = e
else:
    _IMPORT_ERR = None


def list_com_ports() -> List[tuple[str, str]]:
    """Return [(device, description), ...] e.g. ('COM8', 'USB-SERIAL CH340')."""
    if list_ports is None:
        return []
    out: List[tuple[str, str]] = []
    for p in list_ports.comports():
        desc = p.description or ""
        out.append((p.device, desc))
    return sorted(out, key=lambda x: x[0])


class SerialLineReader:
    """
    Background thread reads UTF-8 lines from a serial port.
    Callbacks invoked from the reader thread — marshal to GUI with queue + root.after.
    """

    def __init__(
        self,
        port: str,
        baud: int,
        on_line: Callable[[str], None],
        on_error: Callable[[str], None],
        on_ready: Optional[Callable[[bool, Optional[str]], None]] = None,
    ) -> None:
        if serial is None:
            raise RuntimeError("pyserial not installed") from _IMPORT_ERR
        self._port = port
        self._baud = baud
        self._on_line = on_line
        self._on_error = on_error
        self._on_ready = on_ready
        self._ser: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._run, name="serial_reader", daemon=True)

    @property
    def port(self) -> str:
        return self._port

    @property
    def baud(self) -> int:
        return int(self._baud)

    def start(self) -> None:
        self._t.start()

    def _run(self) -> None:
        buf = bytearray()
        try:
            self._ser = serial.Serial(self._port, self._baud, timeout=0.05)
        except Exception as e:
            msg = f"open {self._port}: {e}"
            if self._on_ready:
                self._on_ready(False, msg)
            else:
                self._on_error(msg)
            return
        if self._on_ready:
            self._on_ready(True, None)
        try:
            while not self._stop.is_set():
                try:
                    chunk = b""
                    with self._lock:
                        if self._ser is not None and self._ser.in_waiting:
                            chunk = self._ser.read(self._ser.in_waiting)
                    if chunk:
                        buf.extend(chunk)
                    else:
                        time.sleep(0.02)
                except Exception as e:
                    if not self._stop.is_set():
                        self._on_error(str(e))
                    break
                while True:
                    nl = buf.find(b"\n")
                    if nl < 0:
                        break
                    raw = bytes(buf[:nl])
                    del buf[: nl + 1]
                    try:
                        line = raw.decode("utf-8", errors="replace").rstrip("\r")
                    except Exception:
                        line = repr(raw)
                    self._on_line(line)
        finally:
            try:
                if self._ser is not None and self._ser.is_open:
                    self._ser.close()
            except Exception:
                pass
            self._ser = None

    def send_line(self, text: str) -> None:
        """Send a CLI line (adds \\r\\n). Safe to call from the GUI thread while the reader thread is running."""
        with self._lock:
            if self._ser is None or not self._ser.is_open:
                raise RuntimeError("serial not open")
            s = text.strip()
            if not s.endswith("\r") and not s.endswith("\n"):
                s += "\r\n"
            self._ser.write(s.encode("utf-8", errors="replace"))

    def stop(self) -> None:
        self._stop.set()
        if self._ser is not None and self._ser.is_open:
            try:
                self._ser.cancel_read()
            except Exception:
                pass
        self._t.join(timeout=2.0)
