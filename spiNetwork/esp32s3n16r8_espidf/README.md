# ESP32-WROOM-1 SPI 主机端实现
esp-idf v5.2.6可以编译通过

## 概述

这个项目实现了 **ESP32-WROOM-1 作为 SPI 主机**,通过 SPI 接口连接到运行 esp-iot-bridge 的 ESP32-S3,从而实现网络访问功能。

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-WROOM-1 (主机)                   │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  应用层 (HTTP/TCP/UDP)                               │  │
│  ├─────────────────────────────────────────────────────┤  │
│  │  LWIP TCP/IP 协议栈                                   │  │
│  ├─────────────────────────────────────────────────────┤  │  │  虚拟网卡驱动                                         │  │
│  ├─────────────────────────────────────────────────────┤  │
│  │  SPI 主机协议栈 (本实现)                               │  │
│  │  - ESP Payload Header 封装/解封装                      │  │
│  │  - SPI 事务管理                                       │  │
│  │  - GPIO 握手信号控制                                   │  │
│  ├─────────────────────────────────────────────────────┤  │
│  │  SPI Master 驱动 (ESP-IDF)                           │  │
│  └─────────────────────────────────────────────────────┘  │
│                          │                                │
│                    SPI 接口 (6线)                          │
│                          │                                │
└──────────────────────────┼────────────────────────────────┘
                           │
┌──────────────────────────┼────────────────────────────────┐
│                          ▼                                │
│                    ESP32-S3 (从机)                        │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  esp-iot-bridge                                     │  │
│  │  - SPI 从机驱动                                     │  │
│  │  - WiFi Station 连接                                │  │
│  │  - 数据包转发桥接                                   │  │
│  └─────────────────────────────────────────────────────┘  │
│                          │                                │
│                    WiFi 接口                              │
│                          │                                │
└──────────────────────────┼────────────────────────────────┘
                           │
                    互联网 (WiFi路由器)
```

## 硬件连接

### SPI 引脚连接

| 功能 | ESP32-WROOM-1 (主机) | ESP32-S3 (从机) | 说明 |
|------|---------------------|-----------------|------|
| MOSI | GPIO 23 (输出) | GPIO 23 (输入) | 主机→从机数据 |
| MISO | GPIO 19 (输入) | GPIO 19 (输出) | 从机→主机数据 |
| SCLK | GPIO 18 (输出) | GPIO 18 (输入) | 时钟信号 |
| CS | GPIO 5 (输出) | GPIO 5 (输入) | 片选信号 |
| Handshake | GPIO 4 (输入) | GPIO 4 (输出) | 从机握手信号 |
| Data Ready | GPIO 2 (输入) | GPIO 2 (输出) | 从机数据就绪 |
| GND | GND | GND | 共地 |
| 3.3V | 3.3V | 3.3V | 电源(可选) |

### 连接示意图
```
ESP32-WROOM-1                ESP32-S3
(主机)                       (从机)
┌───────────┐              ┌───────────┐
│ GPIO23    │──────MOSI────▶│ GPIO23    │
│ GPIO19    │◀────MISO─────│ GPIO19    │
│ GPIO18    │──────CLK─────▶│ GPIO18    │
│ GPIO5     │──────CS──────▶│ GPIO5     │
│ GPIO4     │◀────HS───────│ GPIO4     │
│ GPIO2     │◀────DR───────│ GPIO2     │
│ GND       │──────GND─────│ GND       │
└───────────┘              └───────────┘
```

## 软件架构

### 1. 协议层

#### ESP Payload Header
```c
struct esp_payload_header {
    uint8_t  if_type;       // 接口类型 (0x00=STA, 0x01=AP)
    uint8_t  if_num;        // 接口编号
    uint16_t len;           // 数据长度 (小端序)
    uint16_t offset;        // 数据偏移量 (小端序,通常为16)
    uint16_t checksum;      // 校验和 (小端序)
    uint16_t seq_num;       // 序列号 (小端序)
    uint8_t  flags;         // 标志位
} __attribute__((packed));  // 总16字节
```

#### 数据包格式
```
[ESP Header 16字节][以太网帧 N字节][填充字节对齐到4字节边界]
```

### 2. 通信流程

#### 主机发送数据
```
1. 等待从机 Data Ready = HIGH
2. 拉低 CS 信号
3. 等待从机 Handshake = HIGH
4. 发送 ESP Header + 以太网数据
5. 同时接收从机返回的数据
6. 等待 Handshake = LOW
7. 拉高 CS 信号
8. 处理接收到的数据
```

#### 主机接收数据
```
1. 等待从机 Data Ready = HIGH (表示有数据待接收)
2. 拉低 CS 信号
3. 等待从机 Handshake = HIGH
4. 发送空数据包 (Header中 if_type=0x0F)
5. 同时接收从机发送的数据
6. 等待 Handshake = LOW
7. 拉高 CS 信号
8. 解封装 ESP Header,提取以太网帧
```

### 3. 核心函数

#### SPI 初始化
```c
// 初始化 SPI Master
esp_err_t spi_master_init(void);

// 配置参数
- SPI Mode 2 (CPOL=1, CPHA=0)
- 时钟频率: 10MHz
- DMA 传输
```

#### 数据发送
```c
// 封装并发送网络数据包
int send_packet_to_slave(uint8_t *eth_frame, uint16_t eth_len);
```

#### 数据接收
```c
// 从从机接收网络数据包
int receive_packet_from_slave(uint8_t *eth_frame, uint16_t max_len);
```

## 配置说明

### ESP32-S3 从机配置 (menuconfig)
```
Component config →
  Bridge Configuration →
    [✓] Use SPI interface to provide network data forwarding
    
    SPI Configuration →
      SPI GPIO: MOSI=23, MISO=19, CLK=18, CS=5
      Handshake GPIO: 4
      Data Ready GPIO: 2
      SPI Clock: 10 MHz
```

### ESP32-WROOM-1 主机配置
```c
// 引脚配置 (main.c 开头)
#define SPI_MOSI_PIN        23
#define SPI_MISO_PIN        19
#define SPI_CLK_PIN         18
#define SPI_CS_PIN          5
#define SPI_HANDSHAKE_PIN   4
#define SPI_DATA_READY_PIN  2
```

## 使用方法

### 1. 烧录 ESP32-S3 (从机)
```bash
cd esp-iot-bridge
idf.py set-target esp32s3
idf.py menuconfig  # 启用SPI接口
idf.py build flash
```

### 2. 烧录 ESP32-WROOM-1 (主机)
```bash
cd spiNetwork/esp32_wroom_host
idf.py set-target esp32
idf.py build flash
```

### 3. 硬件连接
按照上面的引脚连接表连接两个ESP32开发板。

### 4. 运行测试
1. 先启动 ESP32-S3 (从机),连接WiFi
2. 再启动 ESP32-WROOM-1 (主机)
3. 观察串口日志,检查SPI通信是否正常
4. 主机将自动发送测试ARP包,验证通信链路

## 调试方法

### 1. 检查 GPIO 状态
```c
// 在主机中添加调试代码
void debug_gpio_state() {
    printf("CS=%d, DR=%d, HS=%d\n",
        gpio_get_level(SPI_CS_PIN),
        gpio_get_level(SPI_DATA_READY_PIN),
        gpio_get_level(SPI_HANDSHAKE_PIN)
    );
}
```

### 2. 抓包分析
使用逻辑分析仪或示波器监控SPI信号:
- CS: 片选信号
- SCLK: 时钟信号
- MOSI/MISO: 数据信号
- Handshake: 握手信号
- Data Ready: 数据就绪信号

### 3. 数据包日志
```c
// 打印发送/接收的数据包
void log_packet(const char *dir, uint8_t *data, int len) {
    printf("%s [%d]: ", dir, len);
    for (int i = 0; i < min(len, 32); i++) {
        printf("%02X ", data[i]);
    }
    printf("...\n");
}
```

## 常见问题

### Q1: SPI 通信失败
**可能原因**:
- 时钟极性/相位不匹配 (应为 Mode 2)
- 时钟频率过高 (先尝试 1MHz)
- 引脚连接错误
- 上下拉电阻配置不当

**解决方案**:
```c
// 降低时钟频率测试
#define SPI_CLK_MHZ 1

// 检查 GPIO 上拉配置
io_conf.pull_up_en = 1;
```

### Q2: 数据包丢失
**可能原因**:
- 缓冲区溢出
- 握手信号时序问题
- CPU负载过高

**解决方案**:
- 增大 RX/TX 队列大小
- 调整延时参数
- 优化中断处理

### Q3: 网络不通
**可能原因**:
- ESP32-S3 WiFi 未连接
- IP 地址配置错误
- 数据包格式错误

**排查步骤**:
1. 检查 ESP32-S3 串口日志,确认WiFi已连接
2. 确认 MAC 地址配置正确
3. 使用 Ping 测试连通性
4. 抓包检查数据包格式

## 性能优化

### 1. DMA 传输
启用 DMA 可大幅提高传输效率:
```c
spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
```

### 2. 批量处理
合并多个小数据包一次性发送,减少SPI事务开销。

### 3. 零拷贝
避免不必要的数据复制,直接使用SPI缓冲区。

## 扩展功能

### 1. 多从机支持
通过软件控制多个CS引脚,支持连接多个SPI从机。

### 2. 中断驱动
使用GPIO中断检测Data Ready信号,提高响应速度。

### 3. 网络桥接
实现完整的网络桥接功能,支持路由、NAT等。

## 参考文档

- [ESP-IDF SPI Master 驱动](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html)
- [esp-iot-bridge SPI 设置](https://github.com/espressif/esp-iot-bridge/blob/master/components/iot_bridge/docs/SPI_setup.md)
- [ESP32 SPI 协议分析](./spi_protocol_analysis.md)

## 许可证

本项目遵循 Apache-2.0 许可证。

## 贡献

欢迎提交 Issue 和 Pull Request。
