/**
 * RP2040 Pico SPI Host - 精简透传模式实现
 * 
 * 功能: 作为SPI主机连接到ESP32-S3从机，无需LWIP协议栈
 * 架构: 主机只发送应用层数据，从机处理所有TCP/IP协议
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

// SPI引脚配置
#define SPI_PORT            spi0
#define PIN_MISO            16      // GPIO16 - SPI0 RX
#define PIN_CS              17      // GPIO17 - CS (软件控制)
#define PIN_SCK             18      // GPIO18 - SPI0 SCK
#define PIN_MOSI            19      // GPIO19 - SPI0 TX
#define PIN_HANDSHAKE       20      // GPIO20 - 从机握手信号 (输入)
#define PIN_DATA_READY      21      // GPIO21 - 从机数据就绪 (输入)

// SPI配置参数
#define SPI_BAUD_RATE       10000000    // 10MHz
#define SPI_BUFFER_SIZE     1600        // 缓冲区大小

// 应用层协议命令定义
#define CMD_NONE            0x00
#define CMD_HTTP_GET        0x01        // HTTP GET请求
#define CMD_HTTP_POST       0x02        // HTTP POST请求
#define CMD_TCP_CONNECT     0x03        // 建立TCP连接
#define CMD_TCP_SEND        0x04        // 发送TCP数据
#define CMD_TCP_CLOSE       0x05        // 关闭TCP连接
#define CMD_MQTT_PUBLISH    0x06        // MQTT发布消息
#define CMD_PING            0x07        // Ping测试
#define CMD_STATUS          0x08        // 获取连接状态
#define CMD_WIFI_SCAN       0x09        // WiFi扫描

// 响应状态码
#define RESP_OK             0x00
#define RESP_ERROR          0x01
#define RESP_PENDING        0x02
#define RESP_TIMEOUT        0x03
#define RESP_DISCONNECTED   0x04

// 数据结构
typedef struct {
    uint8_t  cmd;           // 命令类型
    uint8_t  status;        // 状态码
    uint16_t data_len;      // 数据长度
    uint8_t  data[SPI_BUFFER_SIZE - 4];  // 数据内容
} app_packet_t;

// 统计信息
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;
static uint32_t error_count = 0;

// 缓冲区
static uint8_t tx_buffer[SPI_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t rx_buffer[SPI_BUFFER_SIZE] __attribute__((aligned(4)));

// DMA通道
static int tx_dma_chan = -1;
static int rx_dma_chan = -1;

// 函数声明
static void init_gpio(void);
static void init_spi(void);
static void init_dma(void);
static int spi_transaction(uint8_t *tx_data, uint8_t *rx_data, uint16_t len);
static int send_command(uint8_t cmd, uint8_t *data, uint16_t len);
static int receive_response(app_packet_t *resp, uint32_t timeout_ms);

// HTTP相关函数
static int http_get(const char *url, char *response, uint16_t resp_len);
static int http_post(const char *url, const char *body, char *response, uint16_t resp_len);

// TCP相关函数
static int tcp_connect(const char *host, uint16_t port);
static int tcp_send(const uint8_t *data, uint16_t len);
static void tcp_close(void);

// 初始化GPIO
static void init_gpio(void) {
    // 初始化CS引脚 (输出)
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // 初始高电平
    
    // 初始化握手信号 (输入, 上拉)
    gpio_init(PIN_HANDSHAKE);
    gpio_set_dir(PIN_HANDSHAKE, GPIO_IN);
    gpio_pull_up(PIN_HANDSHAKE);
    
    // 初始化数据就绪信号 (输入, 上拉)
    gpio_init(PIN_DATA_READY);
    gpio_set_dir(PIN_DATA_READY, GPIO_IN);
    gpio_pull_up(PIN_DATA_READY);
    
    printf("[GPIO] Initialized: CS=%d, HS=%d, DR=%d\n", PIN_CS, PIN_HANDSHAKE, PIN_DATA_READY);
}

// 初始化SPI
static void init_spi(void) {
    // 初始化SPI接口
    spi_init(SPI_PORT, SPI_BAUD_RATE);
    
    // 配置SPI引脚
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // 配置SPI参数
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_0, SPI_MSB_FIRST);
    
    printf("[SPI] Initialized: baud=%d Hz, Mode=2\n", SPI_BAUD_RATE);
    printf("[SPI] Pins: MISO=%d, MOSI=%d, SCK=%d\n", PIN_MISO, PIN_MOSI, PIN_SCK);
}

// 初始化DMA
static void init_dma(void) {
    // 申请DMA通道
    tx_dma_chan = dma_claim_unused_channel(true);
    rx_dma_chan = dma_claim_unused_channel(true);
    
    printf("[DMA] Channels: TX=%d, RX=%d\n", tx_dma_chan, rx_dma_chan);
}

// SPI事务: 发送数据并接收响应
// 与ESP32-S3 esp-iot-bridge的时序匹配
static int spi_transaction(uint8_t *tx_data, uint8_t *rx_data, uint16_t len) {
    if (!tx_data || len == 0 || len > SPI_BUFFER_SIZE) {
        return -1;
    }
    
    // 对齐到4字节 (DMA要求)
    uint16_t aligned_len = len;
    if (aligned_len % 4 != 0) {
        aligned_len += 4 - (aligned_len % 4);
    }
    
    // 1. 等待从机数据就绪 (Data Ready = LOW -> 准备好)
    // 注意: 根据具体硬件，极性可能需要调整
    uint32_t timeout = 1000;  // 1秒超时
    while (gpio_get(PIN_DATA_READY) == 1 && timeout-- > 0) {
        sleep_us(100);
    }
    
    if (timeout == 0) {
        printf("[SPI] Timeout waiting for Data Ready\n");
        error_count++;
        return -1;
    }
    
    // 2. 拉低CS开始事务
    gpio_put(PIN_CS, 0);
    sleep_us(10);
    
    // 3. 等待从机握手信号 (Handshake = HIGH)
    timeout = 1000;
    while (gpio_get(PIN_HANDSHAKE) == 0 && timeout-- > 0) {
        sleep_us(10);
    }
    
    if (timeout == 0) {
        gpio_put(PIN_CS, 1);
        printf("[SPI] Timeout waiting for Handshake\n");
        error_count++;
        return -1;
    }
    
    // 4. 使用DMA进行SPI传输 (更高效)
    if (tx_dma_chan >= 0 && rx_dma_chan >= 0) {
        // 配置RX DMA
        dma_channel_config rx_config = dma_channel_get_default_config(rx_dma_chan);
        channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
        channel_config_set_dreq(&rx_config, spi_get_dreq(SPI_PORT, false));
        
        dma_channel_configure(rx_dma_chan, &rx_config,
                            rx_data,          // 写入地址
                            &spi0_hw->ssr,    // 读取地址 (FIFO)
                            aligned_len,      // 传输数量
                            false);           // 不立即启动
        
        // 配置TX DMA
        dma_channel_config tx_config = dma_channel_get_default_config(tx_dma_chan);
        channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
        channel_config_set_dreq(&tx_config, spi_get_dreq(SPI_PORT, true));
        channel_config_set_read_increment(&tx_config, true);
        channel_config_set_write_increment(&tx_config, false);
        
        dma_channel_configure(tx_dma_chan, &tx_config,
                            &spi0_hw->ssr,    // 写入地址 (FIFO)
                            tx_data,          // 读取地址
                            aligned_len,      // 传输数量
                            false);           // 不立即启动
        
        // 启动传输
        dma_start_channel_mask((1u << tx_dma_chan) | (1u << rx_dma_chan));
        
        // 等待传输完成
        dma_channel_wait_for_finish_blocking(tx_dma_chan);
        dma_channel_wait_for_finish_blocking(rx_dma_chan);
    } else {
        // 回退到普通SPI传输
        spi_write_read_blocking(SPI_PORT, tx_data, rx_data, aligned_len);
    }
    
    // 5. 等待握手信号变低 (传输完成)
    timeout = 1000;
    while (gpio_get(PIN_HANDSHAKE) == 1 && timeout-- > 0) {
        sleep_us(10);
    }
    
    // 6. 拉高CS结束事务
    gpio_put(PIN_CS, 1);
    
    return len;
}

// 发送命令到从机
static int send_command(uint8_t cmd, uint8_t *data, uint16_t len) {
    // 封装应用层协议
    app_packet_t *pkt = (app_packet_t *)tx_buffer;
    pkt->cmd = cmd;
    pkt->status = 0;
    pkt->data_len = len;
    
    if (data && len > 0) {
        memcpy(pkt->data, data, len);
    }
    
    // 计算总长度 (头部4字节 + 数据)
    uint16_t total_len = 4 + len;
    
    // 执行SPI传输
    int ret = spi_transaction(tx_buffer, rx_buffer, total_len);
    if (ret < 0) {
        return -1;
    }
    
    tx_count++;
    return 0;
}

// 接收响应
static int receive_response(app_packet_t *resp, uint32_t timeout_ms) {
    if (!resp) {
        return -1;
    }
    
    // 发送空命令查询状态
    uint8_t query[4] = {CMD_STATUS, 0, 0, 0};
    int ret = spi_transaction(query, rx_buffer, sizeof(query));
    if (ret < 0) {
        return -1;
    }
    
    // 解析响应
    memcpy(resp, rx_buffer, sizeof(app_packet_t));
    
    if (resp->data_len > 0) {
        rx_count++;
        return resp->data_len;
    }
    
    return 0;
}

// HTTP GET请求
// 主机不需要处理HTTP协议，只需发送URL，从机处理所有HTTP细节
static int http_get(const char *url, char *response, uint16_t resp_len) {
    if (!url || !response) {
        return -1;
    }
    
    printf("[HTTP] GET %s\n", url);
    
    // 发送HTTP GET命令
    int ret = send_command(CMD_HTTP_GET, (uint8_t *)url, strlen(url));
    if (ret < 0) {
        printf("[HTTP] Failed to send command\n");
        return -1;
    }
    
    // 等待响应 (轮询方式)
    app_packet_t resp;
    uint32_t timeout = 10000;  // 10秒超时
    
    while (timeout-- > 0) {
        int data_len = receive_response(&resp, 1000);
        
        if (data_len > 0) {
            // 收到响应数据
            uint16_t copy_len = data_len < resp_len ? data_len : resp_len;
            memcpy(response, resp.data, copy_len);
            response[copy_len] = '\0';
            
            printf("[HTTP] Response received: %d bytes\n", data_len);
            return data_len;
        }
        
        if (resp.status == RESP_ERROR) {
            printf("[HTTP] Error occurred\n");
            return -1;
        }
        
        sleep_ms(100);  // 等待100ms再查询
    }
    
    printf("[HTTP] Timeout\n");
    return -1;
}

// HTTP POST请求
static int http_post(const char *url, const char *body, char *response, uint16_t resp_len) {
    if (!url || !body) {
        return -1;
    }
    
    printf("[HTTP] POST %s\n", url);
    
    // 封装请求: URL\0BODY
    uint8_t request[SPI_BUFFER_SIZE - 4];
    uint16_t url_len = strlen(url);
    uint16_t body_len = strlen(body);
    
    if (url_len + body_len + 1 > sizeof(request)) {
        printf("[HTTP] Request too large\n");
        return -1;
    }
    
    memcpy(request, url, url_len);
    request[url_len] = '\0';  // 分隔符
    memcpy(request + url_len + 1, body, body_len);
    
    // 发送HTTP POST命令
    int ret = send_command(CMD_HTTP_POST, request, url_len + 1 + body_len);
    if (ret < 0) {
        return -1;
    }
    
    // 等待响应...
    // (与http_get类似，省略)
    
    return 0;
}

// TCP连接
static int tcp_connect(const char *host, uint16_t port) {
    if (!host) {
        return -1;
    }
    
    printf("[TCP] Connecting to %s:%d\n", host, port);
    
    // 封装请求: HOST\0PORT(2字节)
    uint8_t request[256];
    uint16_t host_len = strlen(host);
    
    memcpy(request, host, host_len);
    request[host_len] = '\0';
    request[host_len + 1] = port & 0xFF;
    request[host_len + 2] = (port >> 8) & 0xFF;
    
    int ret = send_command(CMD_TCP_CONNECT, request, host_len + 3);
    if (ret < 0) {
        return -1;
    }
    
    // 等待连接成功
    app_packet_t resp;
    uint32_t timeout = 50;  // 5秒
    
    while (timeout-- > 0) {
        receive_response(&resp, 100);
        
        if (resp.status == RESP_OK) {
            printf("[TCP] Connected\n");
            return 0;
        }
        
        sleep_ms(100);
    }
    
    printf("[TCP] Connection timeout\n");
    return -1;
}

// TCP发送数据
static int tcp_send(const uint8_t *data, uint16_t len) {
    if (!data || len == 0) {
        return -1;
    }
    
    printf("[TCP] Sending %d bytes\n", len);
    
    int ret = send_command(CMD_TCP_SEND, (uint8_t *)data, len);
    if (ret < 0) {
        return -1;
    }
    
    return len;
}

// TCP关闭连接
static void tcp_close(void) {
    printf("[TCP] Closing connection\n");
    send_command(CMD_TCP_CLOSE, NULL, 0);
}

// 传感器数据采集示例
static void sensor_data_upload_task(void) {
    printf("\n[Sensor] Starting data upload task...\n");
    
    // 模拟传感器数据
    float temperature = 25.5f;
    float humidity = 60.0f;
    uint32_t timestamp = to_ms_since_boot(get_absolute_time()) / 1000;
    
    // 构建JSON数据 (非常轻量，不需要JSON库)
    char json_data[256];
    snprintf(json_data, sizeof(json_data),
             "{\"device\":\"rp2040\",\"temp\":%.1f,\"humid\":%.1f,\"time\":%lu}",
             temperature, humidity, timestamp);
    
    printf("[Sensor] Data: %s\n", json_data);
    
    // 通过HTTP POST上传
    char response[512];
    int ret = http_post("http://api.example.com/sensor/data",
                        json_data,
                        response,
                        sizeof(response));
    
    if (ret > 0) {
        printf("[Sensor] Upload successful: %s\n", response);
    } else {
        printf("[Sensor] Upload failed\n");
    }
}

// 主函数
int main(void) {
    // 初始化标准输入输出
    stdio_init_all();
    
    printf("\n========================================\n");
    printf("RP2040 Pico SPI Host - Lightweight Mode\n");
    printf("No LWIP Required - Protocol Offload\n");
    printf("========================================\n\n");
    
    // 等待串口连接
    sleep_ms(2000);
    
    // 初始化各模块
    printf("[Init] Starting...\n");
    init_gpio();
    init_spi();
    init_dma();
    
    printf("[Init] Complete\n");
    printf("[Info] This is a lightweight implementation.\n");
    printf("[Info] All TCP/IP protocol handled by ESP32-S3 slave.\n\n");
    
    // 主循环
    uint32_t loop_count = 0;
    
    while (1) {
        printf("\n--- Loop %lu ---\n", ++loop_count);
        
        // 示例1: HTTP GET请求
        printf("\n[Example 1] HTTP GET request\n");
        char http_resp[1024];
        int ret = http_get("http://www.example.com/api/status",
                           http_resp,
                           sizeof(http_resp));
        
        if (ret > 0) {
            printf("[Result] HTTP Response (%d bytes):\n", ret);
            printf("%.200s...\n", http_resp);  // 只打印前200字符
        }
        
        // 示例2: TCP连接测试
        printf("\n[Example 2] TCP connection test\n");
        ret = tcp_connect("tcpbin.com", 4242);
        if (ret == 0) {
            const char *test_msg = "Hello from RP2040 via SPI!";
            tcp_send((const uint8_t *)test_msg, strlen(test_msg));
            
            // 接收响应
            app_packet_t resp;
            receive_response(&resp, 5000);
            if (resp.data_len > 0) {
                printf("[Result] TCP Response: %.*s\n",
                       resp.data_len, resp.data);
            }
            
            tcp_close();
        }
        
        // 示例3: 传感器数据上传 (每10秒一次)
        if (loop_count % 10 == 0) {
            sensor_data_upload_task();
        }
        
        // 打印统计
        printf("\n[Stats] TX: %lu, RX: %lu, Errors: %lu\n",
               tx_count, rx_count, error_count);
        
        // 延时1秒
        sleep_ms(1000);
    }
    
    return 0;
}
