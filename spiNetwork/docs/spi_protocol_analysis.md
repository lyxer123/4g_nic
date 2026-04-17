# ESP-IoT-Bridge SPI 协议深度分析

## 系统架构

### 角色定义
```
┌─────────────────────┐         ┌─────────────────────┐
│   ESP32-WROOM-1     │         │      ESP32-S3       │
│    (SPI Host)       │◄───────►│   (SPI Slave)       │
│                     │  SPI    │                     │
│  + 虚拟网卡驱动      │         │  + WiFi Station     │
│  + SPI主机协议栈     │         │  + 数据转发桥接      │
│  + 网络数据包处理     │         │  + SPI从机协议栈     │
└─────────────────────┘         └─────────────────────┘
```

### 工作流程
1. **ESP32-S3** (从机): 运行esp-iot-bridge,连接WiFi,等待SPI数据
2. **ESP32-WROOM-1** (主机): 通过SPI发送以太网帧
3. **ESP32-S3**: 接收SPI数据,转发到WiFi网络
4. **ESP32-S3**: 接收WiFi响应,通过SPI返回给主机

## 协议数据包格式

### ESP Payload Header
```c
struct esp_payload_header {
    uint8_t  if_type;      // 接口类型 (STA/AP/Serial等)
    uint8_t  if_num;       // 接口编号
    uint16_t len;          // 数据长度 (小端序)
    uint16_t offset;       // 数据偏移 (小端序)
    uint16_t checksum;     // 校验和 (小端序)
    uint16_t seq_num;      // 序列号 (小端序)
    uint8_t  flags;        // 标志位
} __attribute__((packed));
```

### 接口类型 (if_type)
| 值 | 宏定义 | 用途 |
|-----|---------|------|
| 0x00 | ESP_STA_IF | WiFi Station 数据 |
| 0x01 | ESP_AP_IF | WiFi SoftAP 数据 |
| 0x02 | ESP_SERIAL_IF | 串口透传数据 |
| 0x03 | ESP_HCI_IF | 蓝牙HCI数据 |
| 0x0F | ESP_PRIV_IF | 私有控制命令 |

### 完整数据包结构
```
┌─────────────────────────────────────────────────┐
│            ESP Payload Header                  │
│  (16 bytes: if_type, if_num, len, offset, ...)  │
├─────────────────────────────────────────────────┤
│              Actual Ethernet Frame              │
│         (Destination MAC, Source MAC, ...)       │
├─────────────────────────────────────────────────┤
│                   IP Packet                     │
│              (IP Header + TCP/UDP Data)        │
└─────────────────────────────────────────────────┘
```

## SPI通信时序

### 硬件信号
- **MOSI (GPIO23)**: 主机输出,从机输入 (数据: Host→Slave)
- **MISO (GPIO19)**: 主机输入,从机输出 (数据: Slave→Host)
- **SCLK (GPIO18)**: 时钟信号
- **CS (GPIO5)**: 片选信号 (低电平有效)
- **Handshake (GPIO4)**: 握手信号 (从机→主机)
- **Data Ready (GPIO2)**: 数据就绪 (从机→主机)

### 通信流程
```
1. 从机有数据要发送:
   Slave: Data Ready = HIGH
   
2. 主机检测到Data Ready,开始SPI事务:
   Host: CS = LOW
   
3. 从机检测到CS变低,准备数据:
   Slave: Handshake = HIGH
   
4. 主机检测到Handshake,开始传输:
   Host: SCLK toggle, 全双工数据交换
   
5. 传输完成:
   Slave: Handshake = LOW
   Host: CS = HIGH
```

### 时序图
```
Host:   CS    ─┐  ┌──────────────────────────────┐  ┌──
               └──┘                              └──┘
               
Slave:  DR    ───────┐                 ┌───────────────
                     └─────────────────┘
                     
Slave:  HS    ───────────────┐     ┌───────────────────
                             └─────┘
                             
Host:   CLK   ─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌───
               └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘
```

## 数据流详解

### 主机发送数据 (Host → Slave → Internet)
1. **主机**: 创建以太网帧 (如HTTP请求)
2. **主机**: 封装ESP Payload Header
   - if_type = ESP_STA_IF (0x00)
   - len = 以太网帧长度
   - offset = 16
3. **主机**: 通过SPI发送给从机
4. **从机**: 解封装Header,提取以太网帧
5. **从机**: 通过WiFi Station接口发送到互联网
6. **从机**: 接收响应,通过SPI返回给主机

### 数据包示例: HTTP请求
```
SPI Data from Host to Slave:
┌────────────────────────────────────────────────────────────┐
│ Header (16 bytes)                                          │
│ if_type=0x00, if_num=0x00, len=0x0042, offset=0x0010, ... │
├────────────────────────────────────────────────────────────┤
│ Ethernet Frame (66 bytes)                                  │
│ - Dst MAC: [Router MAC]                                    │
│ - Src MAC: [ESP32-WROOM-1 MAC]                             │
│ - Type: 0x0800 (IPv4)                                      │
│ - IP Header: [Src IP, Dst IP, Protocol=TCP]                │
│ - TCP Header: [Src Port, Dst Port=80, Flags=SYN]          │
│ - Payload: HTTP GET / ...                                  │
└────────────────────────────────────────────────────────────┘
```

## 主机端实现要点

### 1. SPI主机初始化
```c
// 配置SPI主机
spi_bus_config_t buscfg = {
    .mosi_io_num = MOSI_PIN,
    .miso_io_num = MISO_PIN,
    .sclk_io_num = CLK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 1600,
};

spi_device_interface_config_t devcfg = {
    .mode = 2,  // CPOL=1, CPHA=0
    .clock_speed_hz = 10 * 1000 * 1000,  // 10MHz
    .spics_io_num = CS_PIN,
    .queue_size = 3,
};
```

### 2. 数据包封装
```c
int send_network_packet(uint8_t *eth_frame, uint16_t len) {
    // 1. 分配发送缓冲区
    uint8_t tx_buf[1600];
    
    // 2. 填充ESP Header
    struct esp_payload_header *hdr = (struct esp_payload_header *)tx_buf;
    hdr->if_type = ESP_STA_IF;    // WiFi Station接口
    hdr->if_num = 0;
    hdr->len = htole16(len);      // 小端序
    hdr->offset = htole16(16);    // Header长度
    hdr->seq_num = htole16(seq++);
    hdr->flags = 0;
    
    // 3. 复制以太网帧
    memcpy(tx_buf + 16, eth_frame, len);
    
    // 4. 计算校验和 (可选)
    // hdr->checksum = compute_checksum(tx_buf, 16 + len);
    
    // 5. 对齐到4字节边界
    uint16_t total_len = 16 + len;
    if (total_len % 4 != 0) {
        total_len += 4 - (total_len % 4);
    }
    
    // 6. 通过SPI发送
    return spi_transmit(tx_buf, total_len);
}
```

### 3. SPI事务处理
```c
int spi_transmit(uint8_t *tx_data, uint16_t len) {
    // 1. 等待从机的Data Ready信号
    while (gpio_get_level(DATA_READY_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // 2. 拉低CS开始事务
    gpio_set_level(CS_PIN, 0);
    
    // 3. 等待从机Handshake信号
    int timeout = 1000;
    while (gpio_get_level(HANDSHAKE_PIN) == 0 && timeout-- > 0) {
        delay_us(10);
    }
    
    if (timeout <= 0) {
        gpio_set_level(CS_PIN, 1);
        return -1;
    }
    
    // 4. 执行SPI传输 (全双工)
    spi_transaction_t trans = {
        .length = len * 8,  // bits
        .tx_buffer = tx_data,
        .rx_buffer = rx_buffer,  // 同时接收数据
    };
    spi_device_transmit(spi_handle, &trans);
    
    // 5. 等待Handshake变低 (传输完成)
    while (gpio_get_level(HANDSHAKE_PIN) == 1);
    
    // 6. 拉高CS结束事务
    gpio_set_level(CS_PIN, 1);
    
    // 7. 处理接收到的数据
    if (rx_buffer[0] != 0xFF) {  // 不是空数据
        process_received_packet(rx_buffer);
    }
    
    return 0;
}
```

### 4. 虚拟网卡集成 (Linux)
```c
// 在Linux主机上创建虚拟网卡
tun_alloc(char *dev) {
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);
    
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    
    ioctl(fd, TUNSETIFF, (void *)&ifr);
    return fd;
}

// 主循环: 转发数据
void network_bridge_loop() {
    int tun_fd = tun_alloc("ethsta0");
    
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(tun_fd, &fds);
        
        select(tun_fd + 1, &fds, NULL, NULL, NULL);
        
        if (FD_ISSET(tun_fd, &fds)) {
            // 从虚拟网卡读取数据 (来自Linux网络栈)
            uint8_t packet[1600];
            int len = read(tun_fd, packet, sizeof(packet));
            
            // 通过SPI发送到ESP32-S3
            send_network_packet(packet, len);
        }
        
        // 检查SPI接收队列
        if (spi_data_available()) {
            uint8_t rx_packet[1600];
            int rx_len = receive_spi_packet(rx_packet);
            
            // 写入虚拟网卡 (传递给Linux网络栈)
            write(tun_fd, rx_packet, rx_len);
        }
    }
}
```

## 关键配置参数

### ESP32-S3 (从机) 配置
```
CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SPI=y
CONFIG_BRIDGE_EXTERNAL_NETIF_STATION=y

CONFIG_GPIO_MOSI=23
CONFIG_GPIO_MISO=19
CONFIG_GPIO_SCLK=18
CONFIG_GPIO_CS=5
CONFIG_ESP_SPI_GPIO_HANDSHAKE=4
CONFIG_ESP_SPI_GPIO_DATA_READY=2
CONFIG_SPI_CLK_MHZ=10
```

### ESP32-WROOM-1 (主机) 配置
```c
#define SPI_MOSI_PIN    23  // 输出到从机的MOSI
#define SPI_MISO_PIN    19  // 输入从机的MISO
#define SPI_CLK_PIN     18  // 输出时钟
#define SPI_CS_PIN      5   // 输出片选
#define HANDSHAKE_PIN   4   // 输入从机的握手信号
#define DATA_READY_PIN  2   // 输入从机的数据就绪信号
```

## 调试技巧

### 1. 抓包分析
```bash
# 在ESP32-S3上抓包
tcpdump -i spi0 -w /tmp/spi_capture.pcap

# 分析数据包
wireshark /tmp/spi_capture.pcap
```

### 2. GPIO信号分析
```c
// 监控GPIO状态
void monitor_gpio() {
    while (1) {
        printf("CS=%d, DR=%d, HS=%d\n",
            gpio_get_level(CS_PIN),
            gpio_get_level(DATA_READY_PIN),
            gpio_get_level(HANDSHAKE_PIN)
        );
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### 3. 数据包日志
```c
// 打印发送/接收的数据包
void log_packet(const char *prefix, uint8_t *data, int len) {
    printf("%s [%d]: ", prefix, len);
    for (int i = 0; i < min(len, 32); i++) {
        printf("%02X ", data[i]);
    }
    if (len > 32) printf("...");
    printf("\n");
}
```

## 常见问题

### 1. SPI通信失败
- 检查时钟极性/相位 (Mode 2)
- 降低时钟频率测试 (先用1MHz)
- 检查GPIO上拉/下拉配置
- 确认DMA缓冲区对齐 (4字节对齐)

### 2. 数据包丢失
- 增大TX/RX队列大小
- 检查流控制信号
- 监控CPU负载
- 启用硬件CS控制

### 3. 网络不通
- 确认从机WiFi已连接
- 检查MAC地址配置
- 验证DHCP获取的IP
- 使用ping测试连通性

## 性能优化

### 1. DMA传输
```c
// 使用DMA进行大块数据传输
spi_device_interface_config_t devcfg = {
    .flags = SPI_DEVICE_HALFDUPLEX,
    .queue_size = 7,
};
```

### 2. 批量处理
```c
// 批量发送多个小数据包
void batch_send_packets() {
    spi_transaction_ext_t ext = {
        .base.flags = SPI_TRANS_MODE_QIO,
        .base.length = total_len * 8,
        .base.tx_buffer = batched_data,
    };
    spi_device_transmit(spi_handle, (spi_transaction_t *)&ext);
}
```

### 3. 零拷贝优化
```c
// 避免数据复制
uint8_t *tx_buf = spi_get_next_tx_buffer();
fill_ethernet_frame(tx_buf + 16);  // 直接在SPI缓冲区填充
spi_commit_transaction(tx_buf);
```

## 总结

ESP-IoT-Bridge的SPI协议是一个完整的网络桥接方案:
- **物理层**: SPI接口,支持DMA,最高10MHz
- **数据链路层**: 自定义ESP Payload Header封装
- **网络层**: 标准以太网帧转发
- **传输层**: 标准TCP/UDP通过WiFi转发

主机端(ESP32-WROOM-1)需要实现:
1. SPI主机驱动和时序控制
2. ESP协议封装/解封装
3. 虚拟网卡或网络桥接功能
4. 网络数据包处理和转发

这种架构允许任何具有SPI接口的MCU获得WiFi网络能力,而不需要集成WiFi模块。
