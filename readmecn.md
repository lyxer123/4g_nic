# 4G NIC 示例（中文说明）

> 本文件主要用于说明：本工程用于测试 `esp-iot-bridge` 在 **4G（Cat1/PPP）**、**W5500 以太网**、**Wi-Fi** 等场景下的多种联网与转发功能。

## 版本信息

- ESP-IDF：`v5.2.6`（当前项目验证版本）
- ESP-IoT-Bridge：`1.0.2`（组件 `espressif/iot_bridge`）

> 说明：历史调试中也包含 `ESP-IDF v5.1.x` 对照记录，详见 `调试.md`。

| 支持目标 | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-C5 | ESP32-S2 | ESP32-S3 |
| --- | --- | --- | --- | --- | --- | --- | --- |

## 概述

本示例聚焦于网络功能，支持在多个网络接口之间进行数据包转发。设备可通过多种接口（ETH / SPI / SDIO）连接 PC 或 MCU，ESP32 系列芯片通过 NAT 在 PPP 网络接口与其它接口之间转发进出流量。

![4g_nic_topology](https://raw.githubusercontent.com/espressif/esp-iot-bridge/master/components/iot_bridge/docs/_static/4g_nic_en.png)

## 使用方法

### 硬件需求

**必需：**
- 一个 4G 模块
- 一块 ESP32 系列开发板
- 一根用于供电和下载的 Micro-USB 线

**可选：**
- 一根网线（以太网）
- 一根 USB 通信线
- 若干杜邦线（连接 MCU 的 SPI 或 SDIO 接口）

请结合本工程中的配置与文档按需接线。

### 选择 Modem 接口

在 `menuconfig` 中：

`Component config → Bridge Configuration → Modem Configuration`

可选择 Modem 接口类型（UART 或 USB）。

### 选择对外转发接口

在 `menuconfig` 中：

`Component config → Bridge Configuration → The interface used to provide network data forwarding for other devices`

可选择用于连接 PC/MCU 的数据转发接口（ETH / SPI / SDIO）。

### 编译与烧录

执行：

`idf.py flash monitor`

完成编译、烧录和串口监视。

完成一次完整烧录后，可使用：

`idf.py app-flash monitor`

减少后续烧录时间。

> 退出串口监视器：按 `Ctrl-]`。若无法退出，请手动复位开发板。

## 说明

本工程在实际调试中重点验证以下能力：
- 4G 模块（USB/UART）拨号与 PPP 上网
- W5500（SPI Ethernet）链路与转发
- Wi-Fi STA / SoftAP 与桥接场景
- 基于 `esp-iot-bridge` 的单 WAN / 多 LAN、NAT 与接口组合行为

更多信息可参考：
[User_Guide.md](https://github.com/espressif/esp-iot-bridge/blob/master/components/iot_bridge/User_Guide.md)
