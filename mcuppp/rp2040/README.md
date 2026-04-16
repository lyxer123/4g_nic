# RP2040 Arduino PPP 示例

本示例为基于 Arduino 的 RP2040 板子提供 PPP over UART 的串口传输示例。

## 串口连接

- `TX` -> 路由器 PPP RX
- `RX` -> 路由器 PPP TX
- `GND` 共地
- 波特率：`115200`

## 用法

1. 在 Arduino IDE 中打开 `rp2040_ppp.ino`
2. 根据实际板子修改 `PPPRxPin` / `PPPTxPin`
3. 加入 MCU 端 PPP 协议栈（例如 PPPoS 或 lwIP PPP）
4. 启动后，示例会在 PPP 建联后进入网络测试阶段
