/*
 * ESP32-WROOM-1 SPI Host - Simple Arduino Implementation
 * 
 * 功能: 作为SPI主机,通过SPI接口连接到ESP32-S3从机,实现网络数据转发
 * 简化版本: 用于快速验证SPI通信功能
 */

#include <SPI.h>

// SPI引脚配置
#define SPI_MOSI_PIN        23
#define SPI_MISO_PIN        19
#define SPI_CLK_PIN         18
#define SPI_CS_PIN          5
#define SPI_HANDSHAKE_PIN   4
#define SPI_DATA_READY_PIN  2

// SPI参数
#define SPI_CLK_MHZ         10      // 10MHz时钟
#define SPI_BUFFER_SIZE     1600
#define SPI_MODE            SPI_MODE2  // CPOL=1, CPHA=0

// ESP Payload Header 结构
struct esp_payload_header {
    uint8_t  if_type;       // 接口类型
    uint8_t  if_num;        // 接口编号
    uint16_t len;           // 数据长度
    uint16_t offset;        // 数据偏移
    uint16_t checksum;      // 校验和
    uint16_t seq_num;       // 序列号
    uint8_t  flags;         // 标志位
};

// 接口类型
#define ESP_STA_IF          0x00
#define ESP_AP_IF           0x01
#define ESP_SERIAL_IF       0x02

// 统计
uint32_t tx_packets = 0;
uint32_t rx_packets = 0;
uint16_t sequence_num = 0;

// 缓冲区
uint8_t spi_tx_buf[SPI_BUFFER_SIZE];
uint8_t spi_rx_buf[SPI_BUFFER_SIZE];

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    
    Serial.println("ESP32-WROOM-1 SPI Host - Simple Implementation");
    Serial.println("==============================================");
    
    // 初始化GPIO
    pinMode(SPI_CS_PIN, OUTPUT);
    pinMode(SPI_HANDSHAKE_PIN, INPUT_PULLUP);
    pinMode(SPI_DATA_READY_PIN, INPUT_PULLUP);
    
    digitalWrite(SPI_CS_PIN, HIGH);  // CS初始高电平
    
    // 初始化SPI
    SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
    SPI.setFrequency(SPI_CLK_MHZ * 1000000);
    SPI.setDataMode(SPI_MODE);
    SPI.setBitOrder(MSBFIRST);
    
    Serial.println("SPI initialized:");
    Serial.printf("  Mode: %d (CPOL=1, CPHA=0)\n", SPI_MODE);
    Serial.printf("  Clock: %d MHz\n", SPI_CLK_MHZ);
    Serial.printf("  Pins: MOSI=%d, MISO=%d, CLK=%d, CS=%d\n", 
                  SPI_MOSI_PIN, SPI_MISO_PIN, SPI_CLK_PIN, SPI_CS_PIN);
    Serial.printf("  GPIO: HS=%d, DR=%d\n", SPI_HANDSHAKE_PIN, SPI_DATA_READY_PIN);
    
    Serial.println("\nWaiting for ESP32-S3 slave...");
    Serial.println("Please ensure ESP32-S3 is running esp-iot-bridge with SPI interface");
}

// 计算校验和
uint16_t compute_checksum(uint8_t *data, uint16_t len) {
    uint16_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// 小端序转换
uint16_t htole16(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

uint16_t le16toh(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

// 封装ESP数据包
int encapsulate_packet(uint8_t *dst, uint8_t *eth_frame, uint16_t eth_len) {
    if (eth_len > (SPI_BUFFER_SIZE - 16)) {
        return -1;
    }
    
    struct esp_payload_header *hdr = (struct esp_payload_header *)dst;
    
    hdr->if_type = ESP_STA_IF;
    hdr->if_num = 0;
    hdr->len = htole16(eth_len);
    hdr->offset = htole16(16);
    hdr->seq_num = htole16(sequence_num++);
    hdr->flags = 0;
    hdr->checksum = 0;
    
    memcpy(dst + 16, eth_frame, eth_len);
    
    uint16_t total_len = 16 + eth_len;
    hdr->checksum = htole16(compute_checksum(dst, total_len));
    
    // 4字节对齐
    if (total_len % 4 != 0) {
        total_len += 4 - (total_len % 4);
    }
    
    return total_len;
}

// SPI事务: 发送并接收数据
int spi_transaction(uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len) {
    // 1. 等待从机Data Ready
    int timeout = 1000;
    while (digitalRead(SPI_DATA_READY_PIN) == LOW && timeout-- > 0) {
        delay(1);
    }
    
    if (timeout <= 0) {
        Serial.println("[ERROR] Timeout waiting for Data Ready");
        return -1;
    }
    
    // 2. 拉低CS开始事务
    digitalWrite(SPI_CS_PIN, LOW);
    delayMicroseconds(10);
    
    // 3. 等待从机Handshake
    timeout = 1000;
    while (digitalRead(SPI_HANDSHAKE_PIN) == LOW && timeout-- > 0) {
        delayMicroseconds(10);
    }
    
    if (timeout <= 0) {
        digitalWrite(SPI_CS_PIN, HIGH);
        Serial.println("[ERROR] Timeout waiting for Handshake");
        return -1;
    }
    
    // 4. 执行SPI传输
    SPI.beginTransaction(SPISettings(SPI_CLK_MHZ * 1000000, MSBFIRST, SPI_MODE));
    
    for (int i = 0; i < len; i++) {
        rx_buf[i] = SPI.transfer(tx_buf[i]);
    }
    
    SPI.endTransaction();
    
    // 5. 等待Handshake变低
    timeout = 1000;
    while (digitalRead(SPI_HANDSHAKE_PIN) == HIGH && timeout-- > 0) {
        delayMicroseconds(10);
    }
    
    // 6. 结束事务
    digitalWrite(SPI_CS_PIN, HIGH);
    
    return len;
}

// 发送网络数据包
int send_network_packet(uint8_t *eth_frame, uint16_t eth_len) {
    Serial.printf("[TX] Sending packet: %d bytes\n", eth_len);
    
    int total_len = encapsulate_packet(spi_tx_buf, eth_frame, eth_len);
    if (total_len < 0) {
        Serial.println("[ERROR] Failed to encapsulate packet");
        return -1;
    }
    
    int ret = spi_transaction(spi_tx_buf, spi_rx_buf, total_len);
    if (ret < 0) {
        Serial.println("[ERROR] SPI transaction failed");
        return -1;
    }
    
    tx_packets++;
    
    // 检查是否收到数据
    struct esp_payload_header *rx_hdr = (struct esp_payload_header *)spi_rx_buf;
    if (rx_hdr->if_type != 0x0F && le16toh(rx_hdr->len) > 0) {
        rx_packets++;
        uint16_t rx_len = le16toh(rx_hdr->len);
        Serial.printf("[RX] Received packet: %d bytes\n", rx_len);
    }
    
    return eth_len;
}

// 接收网络数据包
int receive_network_packet(uint8_t *eth_frame, uint16_t max_len) {
    // 发送空数据包触发接收
    memset(spi_tx_buf, 0, SPI_BUFFER_SIZE);
    struct esp_payload_header *hdr = (struct esp_payload_header *)spi_tx_buf;
    hdr->if_type = 0x0F;  // 空数据
    hdr->len = 0;
    
    int ret = spi_transaction(spi_tx_buf, spi_rx_buf, 16);
    if (ret < 0) {
        return -1;
    }
    
    // 检查接收数据
    struct esp_payload_header *rx_hdr = (struct esp_payload_header *)spi_rx_buf;
    if (rx_hdr->if_type != 0x0F && le16toh(rx_hdr->len) > 0) {
        uint16_t data_len = le16toh(rx_hdr->len);
        uint16_t offset = le16toh(rx_hdr->offset);
        
        if (offset < 16 || data_len > max_len) {
            Serial.println("[ERROR] Invalid packet received");
            return -1;
        }
        
        memcpy(eth_frame, spi_rx_buf + offset, data_len);
        rx_packets++;
        
        Serial.printf("[RX] Received packet: %d bytes\n", data_len);
        return data_len;
    }
    
    return 0;  // 没有数据
}

// 打印数据包内容
void print_packet(const char *prefix, uint8_t *data, int len) {
    Serial.printf("%s [%d]: ", prefix, len);
    for (int i = 0; i < min(len, 32); i++) {
        Serial.printf("%02X ", data[i]);
    }
    if (len > 32) Serial.print("...");
    Serial.println();
}

// 测试任务计数器
int test_count = 0;

void loop() {
    // 1. 尝试接收数据
    uint8_t rx_frame[SPI_BUFFER_SIZE];
    int rx_len = receive_network_packet(rx_frame, sizeof(rx_frame));
    
    if (rx_len > 0) {
        print_packet("[RX]", rx_frame, rx_len);
    }
    
    // 2. 每5秒发送一个测试包
    if (millis() % 5000 < 100 && test_count < 10) {
        // 构建测试数据包 (简单的ARP请求)
        uint8_t test_packet[] = {
            // 以太网头
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 广播MAC
            0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // 源MAC
            0x08, 0x06,                          // ARP类型
            // ARP数据
            0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,
            0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // 发送方MAC
            0xC0, 0xA8, 0x04, 0x02,               // 发送方IP: 192.168.4.2
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 目标MAC
            0xC0, 0xA8, 0x04, 0x01                // 目标IP: 192.168.4.1
        };
        
        Serial.printf("\n[TEST] Sending test packet #%d...\n", test_count + 1);
        send_network_packet(test_packet, sizeof(test_packet));
        print_packet("[TX]", test_packet, sizeof(test_packet));
        
        test_count++;
    }
    
    // 3. 每10秒打印统计
    static unsigned long last_print = 0;
    if (millis() - last_print > 10000) {
        Serial.printf("\n[STATS] TX: %lu, RX: %lu\n", tx_packets, rx_packets);
        last_print = millis();
    }
    
    // 4. 短暂延时
    delay(10);
}
