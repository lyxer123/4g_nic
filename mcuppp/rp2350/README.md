# RP2350 Arduino PPP 示例

本示例为基于 Arduino 的 RP2350 板子提供 UART PPP 传输层示例。

## 串口连接

- `TX` -> 路由器 PPP RX
- `RX` -> 路由器 PPP TX
- `GND` 共地
- 波特率：`115200`

## 说明

此示例与 RP2040 版结构相同，重点在于串口传输和 PPP 连接触发方式。
实际网络层部分需要接入 RP2350 端的 PPP/网络栈。