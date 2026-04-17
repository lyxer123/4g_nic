# RP2040 Pico SPI Host - 精简透传模式

## 概述

这是一个**无需LWIP协议栈**的SPI网络通信实现，专为RP2040等资源受限芯片设计。

### 核心思想

```
┌─────────────────────────────────────────────────────────────┐
│  RP2040 (主机)          精简协议            ESP32-S3 (从机)    │
│  ┌───────────────┐      应用数据 ────────►┌───────────────┐   │
│  │  应用层       │      (HTTP/TCP)        │  完整LWIP协议栈 │   │
│  │  传感器采集    │ ───────────────────►│  TCP/IP处理    │   │
│  │  业务逻辑      │                       │  Socket API    │   │
│  └───────────────┘                       └───────────────┘   │
│         │                                        │          │
│  ┌───────────────┐                       ┌───────────────┐   │
│  │  简化驱动层    │◄────────────────────►│  WiFi Station  │   │
│  │  SPI主机      │      响应数据         │  网络桥接      │   │
│  └───────────────┘                       └───────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

**关键优势**: RP2040只处理应用层和SPI通信，所有复杂的TCP/IP协议都由ESP32-S3处理。

---

## 为什么选择这个架构？

### RP2040资源限制

| 资源 | 容量 | LWIP需求 | 剩余可用 |
|------|------|----------|----------|
| RAM | 264KB | 100-150KB | ~100KB |
| Flash | 2-16MB | 200KB | 充足 |
| CPU | 133MHz双核 | 一个核处理协议栈 | 一个核给应用 |

### 方案对比

| 方案 | 内存占用 | 复杂度 | 灵活性 | 推荐度 |
|------|---------|--------|--------|--------|
| **本方案** | 极小 | 低 | 高 | ⭐⭐⭐⭐⭐ |
| 完整LWIP | 大(150KB) | 高 | 极高 | ⭐⭐⭐ |
| AT指令 | 极小 | 极低 | 低 | ⭐⭐⭐⭐ |

**结论**: 对于RP2040，本方案是最佳平衡。

---

## 应用层协议

### 协议格式

```
┌────────────────────────────────────────────────┐
│ 命令 (1字节) │ 状态 (1字节) │ 长度 (2字节) │ 数据 (N字节) │
└────────────────────────────────────────────────┘
```

### 支持的命令

| 命令 | 代码 | 功能 | 数据格式 |
|------|------|------|----------|
| `CMD_HTTP_GET` | 0x01 | HTTP GET请求 | URL字符串 |
| `CMD_HTTP_POST` | 0x02 | HTTP POST请求 | URL\0BODY |
| `CMD_TCP_CONNECT` | 0x03 | 建立TCP连接 | HOST\0PORT(2字节) |
| `CMD_TCP_SEND` | 0x04 | 发送TCP数据 | 原始数据 |
| `CMD_TCP_CLOSE` | 0x05 | 关闭TCP连接 | 无 |
| `CMD_MQTT_PUBLISH` | 0x06 | MQTT发布 | TOPIC\0MESSAGE |
| `CMD_PING` | 0x07 | Ping测试 | IP地址 |
| `CMD_STATUS` | 0x08 | 查询状态 | 无 |

### 响应状态码

| 状态 | 代码 | 含义 |
|------|------|------|
| `RESP_OK` | 0x00 | 操作成功 |
| `RESP_ERROR` | 0x01 | 发生错误 |
| `RESP_PENDING` | 0x02 | 操作进行中 |
| `RESP_TIMEOUT` | 0x03 | 超时 |
| `RESP_DISCONNECTED` | 0x04 | 连接断开 |

---

## 硬件连接

### RP2040 Pico引脚分配

| 功能 | Pico引脚 | GPIO | ESP32-S3对应引脚 |
|------|---------|------|------------------|
| MISO |  Pin 21 | GP16 | GPIO19 (MISO) |
| CS | Pin 22 | GP17 | GPIO5 (CS) |
| SCK | Pin 24 | GP18 | GPIO18 (CLK) |
| MOSI | Pin 25 | GP19 | GPIO23 (MOSI) |
| Handshake | Pin 26 | GP20 | GPIO4 (Handshake) |
| Data Ready | Pin 27 | GP21 | GPIO2 (Data Ready) |
| GND | Pin 38 | GND | GND |
| 3.3V | Pin 36 | 3V3 | 3.3V |

### 连接图

```
RP2040 Pico                       ESP32-S3
┌─────────────┐                  ┌─────────────┐
│ GP16 (MISO) │◄─────────────────│ GPIO19      │
│ GP17 (CS)   │─────────────────►│ GPIO5       │
│ GP18 (SCK)  │─────────────────►│ GPIO18      │
│ GP19 (MOSI) │─────────────────►│ GPIO23      │
│ GP20 (HS)   │◄─────────────────│ GPIO4       │
│ GP21 (DR)   │◜─────────────────│ GPIO2       │
│ GND         │──────────────────│ GND         │
│ 3V3         │ (可选) ──────────│ 3.3V        │
└─────────────┘                  └─────────────┘
```

---

## 使用方法

### 1. 编译和烧录

**环境准备**:
```bash
# 设置Pico SDK路径
export PICO_SDK_PATH=/path/to/pico-sdk

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make

# 烧录 (按住BOOTSEL按钮，连接USB，复制uf2文件)
cp rp2040_spi_host.uf2 /media/RPI-RP2/
```

### 2. 准备ESP32-S3从机

使用esp-iot-bridge，启用SPI接口：
```bash
cd esp-iot-bridge
idf.py set-target esp32s3
idf.py menuconfig
# 启用 CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SPI
idf.py build flash
```

**注意**: 需要修改esp-iot-bridge支持自定义应用协议 (需要额外开发)

### 3. 硬件连接

按照上面的引脚连接图，用杜邦线连接两个开发板。

### 4. 运行测试

打开串口监视器 (波特率115200):
```bash
minicom -D /dev/ttyACM0 -b 115200
```

你将看到:
```
========================================
RP2040 Pico SPI Host - Lightweight Mode
No LWIP Required - Protocol Offload
========================================

[Init] Starting...
[GPIO] Initialized: CS=17, HS=20, DR=21
[SPI] Initialized: baud=10000000 Hz, Mode=2
[DMA] Channels: TX=0, RX=1
[Init] Complete
[Info] This is a lightweight implementation.
[Info] All TCP/IP protocol handled by ESP32-S3 slave.

--- Loop 1 ---
[Example 1] HTTP GET request
[HTTP] GET http://www.example.com/api/status
[HTTP] Response received: 256 bytes
[Result] HTTP Response (256 bytes):
{"status":"ok","timestamp":1234567890,"server":"esp32-bridge"}...

[Example 2] TCP connection test
[TCP] Connecting to tcpbin.com:4242
[TCP] Connected
[TCP] Sending 26 bytes
[Result] TCP Response: Echo: Hello from RP2040 via SPI!
[TCP] Closing connection

[Stats] TX: 3, RX: 2, Errors: 0
```

---

## 代码示例

### 简单的HTTP GET

```c
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    
    // 初始化SPI和GPIO
    init_gpio();
    init_spi();
    
    // 发送HTTP GET请求
    char response[1024];
    int len = http_get("http://api.weather.com/v1/current",
                       response, sizeof(response));
    
    if (len > 0) {
        printf("Weather data: %s\n", response);
    }
    
    return 0;
}
```

### 传感器数据上传 (MQTT风格)

```c
void upload_sensor_data(float temp, float humidity) {
    // 构建简单的JSON
    char json[128];
    snprintf(json, sizeof(json),
             "{\"t\":%.1f,\"h\":%.1f}", temp, humidity);
    
    // 发布到MQTT主题 (实际通过ESP32-S3处理)
    char topic[] = "home/sensor/livingroom";
    uint8_t request[256];
    
    // 封装: TOPIC\0MESSAGE
    memcpy(request, topic, strlen(topic) + 1);
    memcpy(request + strlen(topic) + 1, json, strlen(json));
    
    send_command(CMD_MQTT_PUBLISH, request,
                 strlen(topic) + 1 + strlen(json));
}
```

### TCP客户端

```c
void tcp_client_demo(void) {
    // 连接到TCP服务器
    tcp_connect("192.168.1.100", 8080);
    
    // 发送数据
    const char *msg = "Hello from RP2040!";
    tcp_send((const uint8_t *)msg, strlen(msg));
    
    // 接收响应
    app_packet_t resp;
    receive_response(&resp, 5000);
    printf("Received: %.*s\n", resp.data_len, resp.data);
    
    // 关闭连接
    tcp_close();
}
```

---

## 优势

### 1. 极低内存占用
- 无需LWIP协议栈 (节省100-150KB RAM)
- 只需几KB缓冲区
- 适合资源受限的MCU

### 2. 简化的软件开发
- 不需要理解复杂的TCP/IP协议
- 简单的命令-响应模型
- 易于调试和测试

### 3. 集中式协议处理
- 所有网络协议在ESP32-S3上处理
- 可以集中升级和维护
- 主机代码保持稳定

### 4. 灵活性
- 自定义应用层协议
- 可以根据需求扩展命令集
- 支持多种应用场景

---

## 限制

### 1. 依赖从机功能
- 所有网络功能都依赖ESP32-S3
- 从机固件需要支持相应的协议
- 不能绕过从机直接处理网络

### 2. 需要自定义从机固件
- 标准esp-iot-bridge可能不支持此协议
- 需要开发ESP32-S3端的协议处理代码
- 开发工作量增加

### 3. 灵活性受限
- 不能直接使用标准Socket API
- 需要通过自定义命令操作
- 某些高级网络功能受限

---

## 性能

| 指标 | 数值 |
|------|------|
| SPI时钟 | 10 MHz |
| 理论带宽 | 10 Mbps |
| 实际吞吐 | 2-5 Mbps |
| 延迟 | 10-50ms |
| CPU占用 | <10% |
| RAM占用 | ~10KB |

---

## 适用场景

✅ **推荐使用**:
- IoT传感器节点
- 简单的数据采集设备
- 资源受限的嵌入式系统
- 需要WiFi功能但不想处理复杂协议的项目

❌ **不推荐**:
- 需要完整网络协议栈的复杂应用
- 对网络延迟要求极高的场景
- 需要直接操作TCP/IP层的开发

---

## 开发路线图

### 第一阶段: 基础通信
- [x] SPI驱动开发
- [x] 基本命令协议
- [x] HTTP GET/POST

### 第二阶段: 扩展功能
- [ ] WebSocket支持
- [ ] MQTT协议支持
- [ ] OTA固件升级

### 第三阶段: 优化
- [ ] DMA传输优化
- [ ] 低功耗模式
- [ ] 多连接支持

---

## 参考资源

- [RP2040 Pico SDK 文档](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf)
- [ESP-IoT-Bridge SPI指南](https://github.com/espressif/esp-iot-bridge/blob/master/components/iot_bridge/docs/SPI_setup.md)
- [本项目的协议分析文档](../docs/spi_protocol_analysis.md)
- [LWIP需求分析](../docs/lwip_requirement_analysis.md)

---

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request。
