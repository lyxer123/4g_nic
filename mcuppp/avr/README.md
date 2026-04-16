# AVR Arduino PPP 示例

本示例演示在 AVR Arduino（如 ATmega2560）上使用串口与路由器 PPPoS 服务通信。

## 串口连接

- MCU TX -> 路由器 PPP RX
- MCU RX -> 路由器 PPP TX
- GND 共地
- 波特率：115200

## 说明

由于 AVR Arduino 通常没有内置完整的网络栈，本示例提供串口传输层骨架和 PPP 链路状态触发点。
实际网络功能需结合适配的 PPP 协议栈（例如 pppos 库）和网络接口实现。