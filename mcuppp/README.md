# MCU PPP Examples

本目录提供与 `4g_nic` 路由器 PPPoS 服务配合的 MCU 端示例。当前第一个子目录是 `esp32`，针对 `ESP32-WROOM-1` 模块给出两个例子：

- `esp32/arduino`：Arduino 环境下的串口 PPP 传输示例。
- `esp32/esp-idf`：ESP-IDF 环境下的 PPP 客户端示例。
- `stm32/Keil`：Keil/STM32Cube 下的 STM32 UART PPP 客户端示例。
- `stm32/arduino`：STM32 Arduino 串口 PPP 传输示例。
- `stm32/cubemx`：STM32CubeMX/STM32CubeIDE 可直接导入的 STM32F103 PPP 示例。
- `avr/`：AVR Arduino PPP UART 传输示例。
- `pic/`：Microchip PIC UART PPP 客户端示例。
- `nrf52/`：nRF52 Arduino PPP UART 传输示例。
- `rp2040/`：RP2040 Arduino PPP UART 传输示例。
- `rp2350/`：RP2350 Arduino PPP UART 传输示例。
- `generic_ppp/`：通用 lwIP PPP 客户端示例，展示 PPP 建连后 HTTP GET 测试。

## 说明

本仓库路由器端 PPPoS 服务默认使用 `CONFIG_ROUTER_PPP_UART_NUM=2`，即 ESP32-S3 路由器端使用 UART2。

在 MCU 端使用串口与路由器 PPPoS 服务通信时：

- MCU 串口 TX 连接到路由器 PPP RX
- MCU 串口 RX 连接到路由器 PPP TX
- 共地
- 串口速率与路由器端一致，默认 115200

`arduino` 示例提供传输层串口代码，适合作为串口 PPP 客户端的底层。
`esp-idf` 示例则演示如何在 ESP32 MCU 端使用 ESP-IDF PPP 客户端栈。