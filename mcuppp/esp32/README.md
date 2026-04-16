# ESP32 MCU PPP Example

本目录针对 `ESP32-WROOM-1` 模块提供两个示例：

- `arduino`：Arduino 环境下的串口 PPP 传输骨架
- `esp-idf`：ESP-IDF 环境下的 PPP 客户端示例

## PPP 串口说明

当前路由器端 PPPoS 服务默认使用：

- UART 号：`UART2`
- 波特率：`115200`
- 路由器端配置为 `CONFIG_ROUTER_PPP_UART_NUM=2`

ESP32-WROOM-1 侧建议使用 `Serial2`：

- `TX`：连接到路由器 PPP RX
- `RX`：连接到路由器 PPP TX
- `GND`：共地

如果 Arduino 示例中使用的 `Serial2` 默认引脚与板子不同，请显式设置 RX/TX 引脚。