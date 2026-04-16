# STM32 Keil PPP 示例

本示例演示如何在 STM32 上使用 Keil/STM32Cube 的 UART 串口与路由器 PPPoS 服务通信。

## 说明

- 路由器端 PPPoS 服务默认使用 `UART2`。
- STM32 端需要把 UART TX/RX 对接到路由器的 PPP RX/TX，并共地。
- 代码示例采用 `UART2` 和 `lwIP PPP` API，完成 PPP 客户端启动并在获取 IP 后执行网络测试。

## 目标 MCU

- STM32F4、STM32F7、STM32H7 等均可参考本结构。
- 示例中的 `main.c` 使用 `USART2`，实际项目可根据板级设计替换为 `USART1/USART3`。

## 复现流程

1. 在 Keil MDK 中创建工程或导入本目录结构。
2. 添加 HAL、CMSIS、lwIP/PPP 组件。
3. 配置 `UART2` 的 TX/RX 引脚与路由器 PPP UART 连接。
4. 编译并下载。

## 功能

- 建立串口 PPP 连接
- PPP 连接成功后获取本机 IP
- 启动一个简单的 TCP/HTTP 测试（示例代码提供连接 `8.8.8.8:53` 或可扩展为 HTTP GET）
