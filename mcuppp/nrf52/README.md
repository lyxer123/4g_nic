# nRF52 Arduino PPP 示例

本示例演示在 nRF52 Arduino 平台上使用 UART 与路由器 PPPoS 服务对接。

## 串口连接

- MCU TX -> 路由器 PPP RX
- MCU RX -> 路由器 PPP TX
- GND 共地
- 波特率：115200

## 说明

示例提供串口传输层和 PPP 状态触发点，适用于基于 nRF52 的 Arduino 板子（如 Adafruit Feather nRF52）。