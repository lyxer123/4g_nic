# 通用 MCU PPP 客户端示例

本示例适用于任何支持 lwIP PPP 的 MCU，演示如何通过串口与路由器 PPPoS 服务建立 PPP 链路并进行 TCP/HTTP 网络测试。

## 功能

- UART PPPoS 客户端初始化
- PPP 建连回调
- lwIP socket HTTP GET 请求

## 仓库结构

- `main.c`：主程序和网络测试逻辑
- `uart_ppp.h` / `uart_ppp.c`：UART 收发接口和回调处理

## 适用场景

适用于使用 STM32、ESP32、nRF52840 等 MCU，并且已移植 lwIP PPP 库到目标平台的项目。