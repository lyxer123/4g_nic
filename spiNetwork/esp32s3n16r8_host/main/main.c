#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "lwip/lwip_napt.h"
#include <endian.h>
#include "spi_config.h"
#include "esp_hosted_spi_proto.h"

// DMA对齐定义 (来自官方实现)
#define SPI_DMA_ALIGN       4
#define IS_SPI_DMA_ALIGNED(len) (((len) & (SPI_DMA_ALIGN - 1)) == 0)
#define MAKE_SPI_DMA_ALIGNED(len) (((len) + (SPI_DMA_ALIGN - 1)) & ~(SPI_DMA_ALIGN - 1))

// 虚拟网卡配置
#define HOST_MAC_ADDR       {0x02, 0x03, 0x04, 0x05, 0x06, 0x07}

static const char *TAG = "spi_host";

// SPI设备句柄
static spi_device_handle_t spi_handle = NULL;

// 网络接口
static esp_netif_t *eth_netif = NULL;
static esp_eth_handle_t eth_handle = NULL;

// 数据包统计 (扩展以匹配官方统计)
typedef struct {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t checksum_errors;
    uint32_t timeout_errors;
    uint32_t queue_full_count;
    uint32_t spi_trans_errors;
    uint32_t invalid_packets;
} spi_stats_t;

static spi_stats_t stats = {0};
static uint16_t sequence_num = 0;

// 接口类型定义
#define ESP_STA_IF          0x00
#define ESP_AP_IF           0x01
#define ESP_SERIAL_IF       0x02
#define ESP_HCI_IF          0x03
#define ESP_PRIV_IF         0x04

// 内存池管理 (参考官方实现)
typedef struct {
    uint8_t buffer[SPI_BUFFER_SIZE];
    bool in_use;
} spi_buffer_pool_t;

static spi_buffer_pool_t tx_buffer_pool[SPI_BUFFER_POOL_SIZE];
static spi_buffer_pool_t rx_buffer_pool[SPI_BUFFER_POOL_SIZE];
static SemaphoreHandle_t buffer_pool_mutex = NULL;

// 快速缓冲区分配
static uint8_t* alloc_tx_buffer(void)
{
    xSemaphoreTake(buffer_pool_mutex, portMAX_DELAY);
    
    for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
        if (!tx_buffer_pool[i].in_use) {
            tx_buffer_pool[i].in_use = true;
            memset(tx_buffer_pool[i].buffer, 0, SPI_BUFFER_SIZE);
            xSemaphoreGive(buffer_pool_mutex);
            return tx_buffer_pool[i].buffer;
        }
    }
    
    xSemaphoreGive(buffer_pool_mutex);
    ESP_LOGW(TAG, "TX buffer pool exhausted");
    stats.queue_full_count++;
    return NULL;
}

static void free_tx_buffer(uint8_t* buf)
{
    if (!buf) return;
    
    xSemaphoreTake(buffer_pool_mutex, portMAX_DELAY);
    for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
        if (tx_buffer_pool[i].buffer == buf) {
            tx_buffer_pool[i].in_use = false;
            break;
        }
    }
    xSemaphoreGive(buffer_pool_mutex);
}

// 错误处理函数
static void handle_spi_error(const char* context, esp_err_t err)
{
    switch(err) {
        case ESP_ERR_TIMEOUT:
            ESP_LOGW(TAG, "%s: Timeout", context);
            stats.timeout_errors++;
            break;
        case ESP_FAIL:
            ESP_LOGE(TAG, "%s: General failure", context);
            stats.spi_trans_errors++;
            break;
        default:
            ESP_LOGE(TAG, "%s: Error %d", context, err);
            stats.spi_trans_errors++;
            break;
    }
}

// 打印统计信息
static void print_spi_stats(void)
{
    ESP_LOGI(TAG, "=== SPI Statistics ===");
    ESP_LOGI(TAG, "TX: %lu packets, %lu bytes", stats.tx_packets, stats.tx_bytes);
    ESP_LOGI(TAG, "RX: %lu packets, %lu bytes", stats.rx_packets, stats.rx_bytes);
    ESP_LOGI(TAG, "Errors: checksum=%lu, timeout=%lu, queue_full=%lu, spi=%lu, invalid=%lu",
             stats.checksum_errors, stats.timeout_errors, stats.queue_full_count,
             stats.spi_trans_errors, stats.invalid_packets);
}

// 初始化GPIO
static void gpio_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << SPI_CS_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    
    // 输入引脚配置
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << SPI_HANDSHAKE_PIN) | (1ULL << SPI_DATA_READY_PIN);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    
    // 初始状态: CS高电平(无效)
    gpio_set_level(SPI_CS_PIN, 1);
    
    ESP_LOGI(TAG, "GPIO initialized: MOSI=%d, MISO=%d, CLK=%d, CS=%d, HS=%d, DR=%d",
             SPI_MOSI_PIN, SPI_MISO_PIN, SPI_CLK_PIN, SPI_CS_PIN,
             SPI_HANDSHAKE_PIN, SPI_DATA_READY_PIN);
}

// 定义SPI主机 (使用HSPI_HOST或SPI2_HOST以兼容不同ESP-IDF版本)
#ifdef CONFIG_IDF_TARGET_ESP32
    #define SPI_HOST    HSPI_HOST
#else
    #define SPI_HOST    SPI2_HOST
#endif

// 初始化SPI主机 (参考官方实现)
static esp_err_t spi_master_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_BUFFER_SIZE,
    };
    
    // 初始化SPI总线
    esp_err_t ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %d", ret);
        return ret;
    }
    
    // 配置SPI设备 (官方使用Mode 2: CPOL=1, CPHA=0)
    spi_device_interface_config_t devcfg = {
        .mode = 2,
        .clock_speed_hz = SPI_CLK_MHZ * 1000 * 1000,
        .spics_io_num = -1,           // 使用软件CS控制
        .queue_size = SPI_QUEUE_SIZE,
        .flags = 0,
    };
    
    ret = spi_bus_add_device(SPI_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %d", ret);
        return ret;
    }
    
    ESP_LOGI(TAG, "SPI Master initialized on HOST %d: %d MHz, Mode %d", 
             SPI_HOST, SPI_CLK_MHZ, devcfg.mode);
    return ESP_OK;
}

// 计算校验和
static uint16_t compute_checksum(uint8_t *data, uint16_t len)
{
    uint16_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// 封装ESP协议数据包 (使用官方DMA对齐宏)
static int encapsulate_packet(uint8_t *dst, uint8_t *eth_frame, uint16_t eth_len)
{
    if (!dst || !eth_frame || eth_len > (SPI_BUFFER_SIZE - sizeof(struct esp_payload_header))) {
        stats.invalid_packets++;
        return -1;
    }
    
    struct esp_payload_header *hdr = (struct esp_payload_header *)dst;
    memset(hdr, 0, sizeof(*hdr));
    hdr->if_type = ESP_STA_IF;
    hdr->if_num = 0;
    hdr->len = htole16(eth_len);
    hdr->offset = htole16(sizeof(struct esp_payload_header));
    hdr->seq_num = htole16(sequence_num++);
    hdr->flags = 0;
    hdr->checksum = 0;
    
    // 复制以太网帧
    memcpy(dst + sizeof(struct esp_payload_header), eth_frame, eth_len);
    
    // 计算总长度
    uint16_t total_len = sizeof(struct esp_payload_header) + eth_len;
    
    // 计算校验和
    hdr->checksum = htole16(compute_checksum(dst, total_len));
    
    // DMA对齐 (使用官方宏)
    if (!IS_SPI_DMA_ALIGNED(total_len)) {
        total_len = MAKE_SPI_DMA_ALIGNED(total_len);
    }
    
    return total_len;
}

// 解封装ESP协议数据包 (增加校验和验证)
static int decapsulate_packet(uint8_t *src, uint8_t *eth_frame, uint16_t max_len)
{
    if (!src || !eth_frame) {
        stats.invalid_packets++;
        return -1;
    }
    
    struct esp_payload_header *hdr = (struct esp_payload_header *)src;
    
    // 检查Header类型
    if (hdr->if_type != ESP_STA_IF && hdr->if_type != ESP_AP_IF && 
        hdr->if_type != ESP_SERIAL_IF && hdr->if_type != ESP_HCI_IF) {
        ESP_LOGW(TAG, "Unknown if_type: 0x%02X", hdr->if_type);
        stats.invalid_packets++;
        return -1;
    }
    
    uint16_t data_len = le16toh(hdr->len);
    uint16_t offset = le16toh(hdr->offset);
    
    // 验证长度
    if (data_len == 0 || offset < sizeof(struct esp_payload_header) || 
        data_len > max_len || data_len > SPI_BUFFER_SIZE) {
        ESP_LOGW(TAG, "Invalid packet: offset=%d, len=%d", offset, data_len);
        stats.invalid_packets++;
        return -1;
    }
    
    uint16_t rx_checksum = le16toh(hdr->checksum);
    if (rx_checksum != 0) {
        hdr->checksum = 0;
        uint16_t calc_checksum = compute_checksum(src, offset + data_len);
        hdr->checksum = htole16(rx_checksum);
        if (calc_checksum != rx_checksum) {
            ESP_LOGW(TAG, "Checksum mismatch: calc=%04X, rx=%04X", calc_checksum, rx_checksum);
            stats.checksum_errors++;
            return -1;
        }
    }
    
    // 复制以太网帧
    memcpy(eth_frame, src + offset, data_len);
    
    return data_len;
}

// SPI事务: 发送数据并接收响应
static int spi_transaction(uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len)
{
    (void)len;
    if (!spi_handle || !tx_buf || !rx_buf) {
        return -1;
    }
    
    // 等待从机数据就绪信号
    int timeout = 1000;  // 1秒超时
    while (gpio_get_level(SPI_DATA_READY_PIN) == 0 && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (timeout <= 0) {
        ESP_LOGW(TAG, "Timeout waiting for Data Ready");
        return -1;
    }
    
    // 软件控制CS (拉低开始事务)
    gpio_set_level(SPI_CS_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));  // 短暂延时
    
    // 等待从机握手信号
    timeout = 1000;
    while (gpio_get_level(SPI_HANDSHAKE_PIN) == 0 && timeout-- > 0) {
        esp_rom_delay_us(10);
    }
    
    if (timeout <= 0) {
        gpio_set_level(SPI_CS_PIN, 1);
        ESP_LOGW(TAG, "Timeout waiting for Handshake");
        return -1;
    }
    
    spi_transaction_t trans = {
        .length = SPI_BUFFER_SIZE * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    
    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    if (ret != ESP_OK) {
        gpio_set_level(SPI_CS_PIN, 1);
        ESP_LOGE(TAG, "SPI transmit failed: %d", ret);
        return -1;
    }
    
    // 等待握手信号变低
    timeout = 1000;
    while (gpio_get_level(SPI_HANDSHAKE_PIN) == 1 && timeout-- > 0) {
        esp_rom_delay_us(10);
    }
    
    // 结束事务 (拉高CS)
    gpio_set_level(SPI_CS_PIN, 1);
    
    return SPI_BUFFER_SIZE;
}

// 发送网络数据包到从机 (使用内存池)
static int send_packet_to_slave(uint8_t *eth_frame, uint16_t eth_len)
{
    if (!eth_frame || eth_len == 0) {
        return -1;
    }
    
    // 从内存池分配缓冲区
    uint8_t *tx_buf = alloc_tx_buffer();
    uint8_t *rx_buf = alloc_tx_buffer();
    if (!tx_buf || !rx_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        if (tx_buf) free_tx_buffer(tx_buf);
        if (rx_buf) free_tx_buffer(rx_buf);
        return -1;
    }
    
    // 封装数据
    int total_len = encapsulate_packet(tx_buf, eth_frame, eth_len);
    if (total_len < 0) {
        ESP_LOGE(TAG, "Failed to encapsulate packet");
        free_tx_buffer(tx_buf);
        free_tx_buffer(rx_buf);
        return -1;
    }
    
    // SPI传输
    int ret = spi_transaction(tx_buf, rx_buf, SPI_BUFFER_SIZE);
    if (ret < 0) {
        ESP_LOGW(TAG, "SPI transaction failed");
        free_tx_buffer(tx_buf);
        free_tx_buffer(rx_buf);
        return -1;
    }
    
    // 更新统计
    stats.tx_packets++;
    stats.tx_bytes += eth_len;
    
    // 处理接收到的数据
    struct esp_payload_header *hdr = (struct esp_payload_header *)rx_buf;
    uint16_t rx_len = le16toh(hdr->len);
    if (hdr->if_type != ESP_HOSTED_DUMMY_IF_TYPE && rx_len > 0) {
        stats.rx_packets++;
        stats.rx_bytes += rx_len;
        
        ESP_LOGD(TAG, "Received %d bytes from slave", rx_len);
    }
    
    // 释放缓冲区
    free_tx_buffer(tx_buf);
    free_tx_buffer(rx_buf);
    
    return eth_len;
}

// 从从机接收数据包 (使用内存池)
static int receive_packet_from_slave(uint8_t *eth_frame, uint16_t max_len)
{
    // 从内存池分配缓冲区
    uint8_t *tx_buf = alloc_tx_buffer();
    uint8_t *rx_buf = alloc_tx_buffer();
    if (!tx_buf || !rx_buf) {
        if (tx_buf) free_tx_buffer(tx_buf);
        if (rx_buf) free_tx_buffer(rx_buf);
        return -1;
    }
    
    // 发送空数据包以触发接收
    memset(tx_buf, 0, SPI_BUFFER_SIZE);
    struct esp_payload_header *hdr = (struct esp_payload_header *)tx_buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->if_type = ESP_HOSTED_DUMMY_IF_TYPE;
    hdr->if_num = ESP_HOSTED_DUMMY_IF_TYPE;
    hdr->len = 0;
    
    int ret = spi_transaction(tx_buf, rx_buf, SPI_BUFFER_SIZE);
    if (ret < 0) {
        free_tx_buffer(tx_buf);
        free_tx_buffer(rx_buf);
        return -1;
    }
    
    // 检查接收到的数据
    hdr = (struct esp_payload_header *)rx_buf;
    uint16_t rx_len = le16toh(hdr->len);
    if (hdr->if_type != ESP_HOSTED_DUMMY_IF_TYPE && rx_len > 0) {
        // 解封装数据包
        int eth_len = decapsulate_packet(rx_buf, eth_frame, max_len);
        if (eth_len > 0) {
            stats.rx_packets++;
            stats.rx_bytes += eth_len;
            free_tx_buffer(tx_buf);
            free_tx_buffer(rx_buf);
            return eth_len;
        }
    }
    
    free_tx_buffer(tx_buf);
    free_tx_buffer(rx_buf);
    return 0;  // 没有数据
}

// 虚拟以太网接收回调
static esp_err_t eth_input_callback(esp_eth_handle_t eth_handle, uint8_t *buffer, uint32_t length, void *priv)
{
    // 这个回调会在以太网驱动接收到数据时调用
    // 在这里,我们需要将数据通过SPI发送给从机
    
    int ret = send_packet_to_slave(buffer, length);
    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to send packet to slave");
    }
    
    return ESP_OK;
}

// 初始化虚拟网卡
static esp_err_t init_virtual_eth(void)
{
    // 创建网络接口
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&netif_cfg);
    if (!eth_netif) {
        ESP_LOGE(TAG, "Failed to create netif");
        return ESP_FAIL;
    }
    
    // 设置MAC地址
    uint8_t mac_addr[] = HOST_MAC_ADDR;
    esp_netif_set_mac(eth_netif, mac_addr);
    
    // 配置IP (静态IP或DHCP)
    esp_netif_ip_info_t ip_info = {
        .ip = { .addr = ESP_IP4TOADDR(192, 168, 4, 2) },
        .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
        .gw = { .addr = ESP_IP4TOADDR(192, 168, 4, 1) },
    };
    esp_netif_set_ip_info(eth_netif, &ip_info);
    
    // 创建虚拟以太网驱动
    esp_eth_config_t eth_config = {
        .mac = NULL,  // 虚拟MAC
        .phy = NULL,  // 虚拟PHY
        .check_link_period_ms = 1000,
        .stack_input = eth_input_callback,
    };
    
    eth_handle = esp_eth_new(&eth_config);
    if (!eth_handle) {
        ESP_LOGE(TAG, "Failed to create eth handle");
        return ESP_FAIL;
    }
    
    // 连接网络接口和以太网驱动
    esp_netif_attach(eth_netif, eth_handle);
    
    // 启动网络接口
    esp_netif_action_start(eth_netif, NULL, 0, NULL);
    
    ESP_LOGI(TAG, "Virtual Ethernet initialized: MAC %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    ESP_LOGI(TAG, "IP: 192.168.4.2, GW: 192.168.4.1");
    
    return ESP_OK;
}

// 主任务: 网络数据包转发
static void network_bridge_task(void *pvParameters)
{
    uint8_t rx_eth_frame[SPI_BUFFER_SIZE];
    
    ESP_LOGI(TAG, "Network bridge task started");
    
    while (1) {
        // 1. 尝试从从机接收数据
        int rx_len = receive_packet_from_slave(rx_eth_frame, sizeof(rx_eth_frame));
        if (rx_len > 0) {
            ESP_LOGI(TAG, "Received packet from slave: %d bytes", rx_len);
            
            // 将数据传递给虚拟网卡
            // esp_netif_receive(eth_netif, rx_eth_frame, rx_len, NULL);
            
            // 打印数据包信息 (调试用)
            ESP_LOG_BUFFER_HEX("RX Packet", rx_eth_frame, (rx_len > 32) ? 32 : rx_len);
        }
        
        // 2. 检查是否需要发送数据 (从网络栈获取)
        // 这部分应该在eth_input_callback中处理
        
        // 3. 打印统计信息
        static uint32_t last_print = 0;
        if (xTaskGetTickCount() - last_print > pdMS_TO_TICKS(10000)) {
            print_spi_stats();
            last_print = xTaskGetTickCount();
        }
        
        // 4. 短暂延时
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 测试任务: 发送测试数据包
static void test_tx_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));  // 等待系统初始化
    
    // 构建ARP请求包 (用于测试)
    uint8_t arp_packet[] = {
        // 以太网头
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 目的MAC (广播)
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // 源MAC
        0x08, 0x06,                          // 类型: ARP
        // ARP数据
        0x00, 0x01,                          // 硬件类型: Ethernet
        0x08, 0x00,                          // 协议类型: IPv4
        0x06,                                // 硬件地址长度
        0x04,                                // 协议地址长度
        0x00, 0x01,                          // 操作: 请求
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // 发送方MAC
        192, 168, 4, 2,                       // 发送方IP
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 目标MAC (未知)
        192, 168, 4, 1,                       // 目标IP
    };
    
    ESP_LOGI(TAG, "Starting test transmission...");
    
    for (int i = 0; i < 5; i++) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        int ret = send_packet_to_slave(arp_packet, sizeof(arp_packet));
        if (ret > 0) {
            ESP_LOGI(TAG, "Test packet %d sent successfully", i + 1);
        } else {
            ESP_LOGW(TAG, "Failed to send test packet %d", i + 1);
        }
    }
    
    ESP_LOGI(TAG, "Test transmission completed");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-WROOM-1 SPI Host - Network Bridge");
    ESP_LOGI(TAG, "========================================");
    
    // 初始化GPIO
    gpio_init();
    
    // 初始化SPI主机
    ESP_ERROR_CHECK(spi_master_init());
    
    // 初始化互斥锁
    buffer_pool_mutex = xSemaphoreCreateMutex();
    if (!buffer_pool_mutex) {
        ESP_LOGE(TAG, "Failed to create buffer pool mutex");
        return;
    }
    
    // 初始化网络栈
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 初始化虚拟网卡
    ESP_ERROR_CHECK(init_virtual_eth());
    
    // 创建网络桥接任务
    xTaskCreate(network_bridge_task, "net_bridge", 4096, NULL, 5, NULL);
    
    // 创建测试任务 (可选)
    xTaskCreate(test_tx_task, "test_tx", 2048, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "Initialization complete. Waiting for SPI slave...");
    ESP_LOGI(TAG, "Connect to ESP32-S3 running esp-iot-bridge with SPI interface");
}
