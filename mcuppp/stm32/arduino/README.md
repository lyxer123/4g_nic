# STM32 Arduino PPP 示例

本示例演示如何在 STM32 Arduino 环境下使用 UART 串口与路由器 PPPoS 服务对接。

## 串口连接

- MCU TX -> 路由器 PPP RX
- MCU RX -> 路由器 PPP TX
- GND 共地
- 波特率：115200

## 说明

本示例提供 UART 传输层骨架，适用于基于 STM32 Arduino 栈的开发板（如 STM32F103、STM32F4 等）。实际 PPP 协议栈需在本示例基础上接入 `pppos` 或 lwIP PPP。
