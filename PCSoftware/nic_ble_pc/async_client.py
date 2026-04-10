"""Async BLE operations (bleak). Used from a dedicated event-loop thread."""

from __future__ import annotations

import asyncio
import json
from typing import Callable, List, Optional, Tuple

from bleak import BleakClient, BleakScanner

from .protocol import CHAR_RX_UUID, CHAR_TX_UUID, DEVICE_NAME_HINTS, SERVICE_UUID


ScanRow = Tuple[Optional[str], str]  # (name, address)


def _matches_device_name(name: Optional[str]) -> bool:
    if not name:
        return False
    return any(h in name for h in DEVICE_NAME_HINTS)


async def scan_devices(timeout_s: float = 8.0) -> List[ScanRow]:
    """Prefer service UUID filter; fall back to advertisement name hints."""
    out: dict[str, Optional[str]] = {}
    try:
        devs = await BleakScanner.discover(timeout=timeout_s, service_uuids=[SERVICE_UUID])
        for d in devs:
            out[d.address] = d.name
    except TypeError:
        pass
    if not out:
        for d in await BleakScanner.discover(timeout=timeout_s):
            if _matches_device_name(d.name):
                out[d.address] = d.name
    return sorted(((n, a) for a, n in out.items()), key=lambda x: (x[0] or "", x[1]))


class NicBleSession:
    """One connected peripheral; call methods only on the BLE event-loop thread."""

    def __init__(self, address: str, on_notify_line: Callable[[str], None]) -> None:
        self._address = address
        self._on_notify_line = on_notify_line
        self._client: Optional[BleakClient] = None
        self._notify_set = False

    @property
    def address(self) -> str:
        return self._address

    def _handler(self, _char_uuid: str, data: bytearray) -> None:
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = repr(data)
        self._on_notify_line(text)

    async def connect(self) -> None:
        if self._client is not None:
            await self.disconnect()
        self._client = BleakClient(self._address)
        await self._client.connect()
        assert self._client.is_connected
        await self._client.start_notify(CHAR_TX_UUID, self._handler)
        self._notify_set = True

    async def disconnect(self) -> None:
        if self._client is None:
            return
        try:
            if self._notify_set and self._client.is_connected:
                await self._client.stop_notify(CHAR_TX_UUID)
        except Exception:
            pass
        self._notify_set = False
        try:
            await self._client.disconnect()
        except Exception:
            pass
        self._client = None

    async def write_json(self, obj: dict, *, with_response: bool = False) -> None:
        if self._client is None or not self._client.is_connected:
            raise RuntimeError("not connected")
        payload = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        await self._client.write_gatt_char(CHAR_RX_UUID, payload, response=with_response)

    @staticmethod
    def pretty_cmd_examples() -> str:
        return "\n".join(
            [
                '{"cmd":"ping"}',
                '{"cmd":"get_mode"}',
                '{"cmd":"set_mode","work_mode_id":2}',
                '{"cmd":"version"}',
            ]
        )
