# BLE 设置协议（ESP32-S3 ↔ 手机）

固件通过 **ESP-IDF Bluedroid** 暴露 **GATT 从机**（与 `sdkconfig` 中 `CONFIG_BT_BLUEDROID_ENABLED=y` 一致）。

## GATT 布局

| 项 | UUID（16 位） | 属性 |
|------|----------------|------------|
| 服务 | `0xFF50` | — |
| RX（手机 → 设备） | `0xFF51` | 写、无响应写 |
| TX（设备 → 手机） | `0xFF52` | 读、通知 |

广播名称：**`4G_NIC_CFG`**。广播数据中包含 16 位服务 UUID `0xFF50`（在 UUID 字段中为 little-endian 字节 `50 FF`）。

## 帧格式

1. 手机向 **RX**（`0xFF51`）写入 **一条 UTF-8 JSON 对象**。在协商 MTU 前，建议单次负载尽量短（≤ 约 380 字节）。
2. 设备解析 JSON，通过 **TX**（`0xFF52`）以 **通知（notification）** 形式下发 **应答**。
3. 完成 **MTU 交换** 后，可承载更长的 JSON；在此之前请按 **ATT MTU 23** 估算（单次通知载荷约 20 字节）。固件会在必要时截断；若应答将变长，可自行增加分片协议。

## 命令（RX 侧 JSON）

所有消息均包含字符串字段 `"cmd"`。

| `cmd` | 附加字段 | TX 通知（示例） |
|-------|----------------|---------------------------|
| `ping` | — | `{"ok":true,"cmd":"ping","device":"4g_nic"}` |
| `get_mode` | — | `{"ok":true,"work_mode_id":1}` |
| `set_mode` | `work_mode_id`（数字） | `{"ok":true,"cmd":"set_mode","work_mode_id":2}` 或 `{"ok":false,"error":"apply_failed","esp":258}` |
| `version` | — | `{"ok":true,"version":"…"}`（来自应用描述符） |

`set_mode` 的校验与延期生效路径与网页端 / `web_service_apply_work_mode_id()` 一致。

## 安全说明

本版本 **未** 增加应用层鉴权。无线范围内任意设备均可连接并发送命令。若用于生产环境，请增加配对/绑定策略、经鉴权通道，或在 JSON 中加入共享密钥并配合限流。

## Wi-Fi / BLE 共存

ESP32-S3 上 BLE 与 Wi-Fi 可同时工作；排查共存问题时注意固件日志。若 Flash/RAM 紧张，可在 `menuconfig` 中裁减未使用的 Bluedroid 选项。

---

串口 UART 子命令与 BLE 对照见 **[命令.md](命令.md)**。
