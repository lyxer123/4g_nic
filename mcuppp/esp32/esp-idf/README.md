# ESP-IDF PPP 客户端示例

这个示例展示了在 `ESP32-WROOM-1` 上使用 ESP-IDF 的 PPP 客户端与路由器 PPPoS 服务对接。

## 连接说明

- MCU TX -> 路由器 PPP RX
- MCU RX -> 路由器 PPP TX
- GND 共地

## 要点

- 使用 UART2，波特率 115200
- 如果没有自动识别，请在 `menuconfig` 中开启 `CONFIG_LWIP_PPP_SUPPORT`
- 如果需要用户名/密码认证，可在 `esp_netif_ppp_set_auth` 中改为 `NETIF_PPP_AUTHTYPE_PAP` / `NETIF_PPP_AUTHTYPE_CHAP`，并提供账号密码。