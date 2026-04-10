"""HTTP JSON client for device Web UI APIs (same paths as webPage/js/app.js)."""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any, Optional


class WebApiError(Exception):
    def __init__(self, message: str, *, status: Optional[int] = None, body: Optional[str] = None) -> None:
        super().__init__(message)
        self.status = status
        self.body = body


def normalize_base_url(url: str) -> str:
    u = (url or "").strip()
    if not u:
        raise WebApiError("设备 Web 基址为空，请填写例如 http://192.168.5.1")
    u = u.rstrip("/")
    if not u.startswith("http://") and not u.startswith("https://"):
        u = "http://" + u
    return u


class WebApiClient:
    """Minimal client: no browser cookies needed (firmware APIs are open after UI login check removed server-side)."""

    def __init__(self, base_url: str, *, timeout_s: float = 12.0) -> None:
        self.base = normalize_base_url(base_url)
        self.timeout_s = timeout_s

    def _request(
        self,
        method: str,
        path: str,
        *,
        json_obj: Optional[dict[str, Any]] = None,
        data: Optional[bytes] = None,
    ) -> Any:
        if not path.startswith("/"):
            path = "/" + path
        url = self.base + path
        headers = {"Accept": "application/json"}
        body: Optional[bytes] = None
        if json_obj is not None:
            body = json.dumps(json_obj, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json; charset=utf-8"
        elif data is not None:
            body = data
        req = urllib.request.Request(url, data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout_s) as resp:
                raw = resp.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as e:
            raw = e.read().decode("utf-8", errors="replace") if e.fp else ""
            try:
                j = json.loads(raw) if raw else {}
                msg = str(j.get("message") or j.get("status") or raw or e.reason)
            except json.JSONDecodeError:
                msg = raw or str(e.reason)
            raise WebApiError(msg, status=e.code, body=raw) from e
        except urllib.error.URLError as e:
            raise WebApiError(f"网络错误: {e.reason}") from e
        if not raw.strip():
            return None
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return {"raw": raw}

    def get(self, path: str) -> Any:
        return self._request("GET", path)

    def post_json(self, path: str, obj: dict[str, Any]) -> Any:
        return self._request("POST", path, json_obj=obj)

    def delete(self, path: str) -> Any:
        return self._request("DELETE", path)
