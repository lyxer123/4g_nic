# Microchip PIC PPP 示例

本示例展示如何在 Microchip PIC 平台上通过 UART 与路由器 PPPoS 服务对接。

## 说明

- 路由器端 PPPoS 默认使用 UART2，波特率 115200。
- PIC 端通常使用 `USART` 串口连接到路由器 PPP 接口。
- 示例适用于 MPLAB XC8 + PIC18 体系，提供 UART 传输层骨架。
- 要实现完整网络通信，需在 PIC 端移植或使用轻量级 PPP 客户端和 TCP/IP 协议栈。

## 资源说明

- 典型 PIC18F/AVR 的 RAM 容量有限，适合做 PPP 链路层或简单的 PPPoS 客户端。
- 如果要跑 HTTP/网页抓取，建议选择更大内存的 MCU（如 PIC24、dsPIC、PIC32）或使用外部网络模块。

## 串口连接

- PIC TX -> 路由器 PPP RX
- PIC RX -> 路由器 PPP TX
- GND 共地

## 目录结构

- `pic18_ppp.c`：PIC18 主程序示例
- `pic18_ppp.h`：头文件
- `README.md`：本说明文件
