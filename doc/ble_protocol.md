# BLE settings protocol (ESP32-S3 ↔ phone)

Firmware exposes a **GATT peripheral** using **ESP-IDF Bluedroid** (same as `CONFIG_BT_BLUEDROID_ENABLED=y` in `sdkconfig`).

## GATT layout

| Item | UUID (16-bit) | Properties |
|------|----------------|------------|
| Service | `0xFF50` | — |
| RX (phone → device) | `0xFF51` | Write, Write Without Response |
| TX (device → phone) | `0xFF52` | Read, Notify |

Advertising name: **`4G_NIC_CFG`**. The advertisement includes the 16-bit service UUID `0xFF50` (little-endian bytes `50 FF` in the UUID field).

## Framing

1. Phone writes a **single UTF-8 JSON object** to **RX** (`0xFF51`). Keep payloads short (≤ ~380 bytes recommended before MTU negotiation).
2. Device parses JSON and sends the **response** as a **notification** on **TX** (`0xFF52`).
3. After **MTU exchange**, larger JSON responses may fit; until then assume **ATT MTU 23** (≈20 bytes payload per notification). The firmware truncates if needed; extend with chunking if you add long replies.

## Commands (RX JSON)

All messages include a string field `"cmd"`.

| `cmd` | Extra fields | TX notification (example) |
|-------|----------------|---------------------------|
| `ping` | — | `{"ok":true,"cmd":"ping","device":"4g_nic"}` |
| `get_mode` | — | `{"ok":true,"work_mode_id":1}` |
| `set_mode` | `work_mode_id` (number) | `{"ok":true,"cmd":"set_mode","work_mode_id":2}` or `{"ok":false,"error":"apply_failed","esp":258}` |
| `version` | — | `{"ok":true,"version":"…"}` (from app descriptor) |

`set_mode` uses the same validation and deferred apply path as the web UI / `web_service_apply_work_mode_id()`.

## Security note

This revision does **not** add application-level authentication. Anyone in radio range can connect and send commands. For production, add pairing/bonding policy, an authenticated channel, or at least a shared secret in JSON plus rate limiting.

## Wi-Fi / BLE coexistence

BLE and Wi-Fi run together on ESP32-S3; keep firmware logs in mind if diagnosing coexistence issues. If flash/RAM is tight, trim unused Bluedroid features in `menuconfig`.
