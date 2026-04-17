/**
 * ESP32-WROOM-1 SPI Host - 中断驱动优化版本
 * 
 * 基于官方Linux驱动实现的中断驱动架构
 * 大幅降低CPU占用，提高响应速度
 */

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

// SPI引脚配置
#define SPI_MOSI_PIN        23
#define SPI_MISO_PIN        19
#define SPI_CLK_PIN         18
#define SPI_CS_PIN          5
#define SPI_HANDSHAKE_PIN   4
#define SPI_DATA_READY_PIN  2

// SPI配置参数
#define SPI_CLK_MHZ         10
#define SPI_BUFFER_SIZE     1600
#define SPI_QUEUE_SIZE      100
#define SPI_BUFFER_POOL_SIZE 8
#define MAX_PRIORITY_QUEUES 3

// 优先级定义
#define PRIO_Q_HIGH         0   // 控制命令
#define PRIO_Q_NORMAL       1   // TCP/UDP数据
#define PRIO_Q_LOW          2   // 日志、调试

// 流控参数
#define TX_MAX_PENDING      100
#define TX_RESUME_THRESHOLD 20

// 虚拟网卡配置
#define HOST_MAC_ADDR       {0x02, 0x03, 0x04, 0x05, 0x06, 0x07}

static const char *TAG = "spi_host_irq";

// SPI设备句柄
static spi_device_handle_t spi_handle = NULL;

// 网络接口
static esp_netif_t *eth_netif = NULL;
static esp_eth_handle_t eth_handle = NULL;

// 统计
static struct {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t checksum_errors;
    uint32_t timeout_errors;
    uint32_t queue_full_count;
    uint32_t spi_trans_errors;
    uint32_t invalid_packets;
    uint32_t interrupts_count;
} stats = {0};

static uint16_t sequence_num = 0;

// 数据路径控制
static volatile bool data_path_open = false;

// 流控
static volatile bool tx_paused = false;
static volatile uint32_t tx_pending = 0;
static SemaphoreHandle_t tx_flow_sem = NULL;

// ESP Payload Header
struct esp_payload_header {
    uint8_t  if_type;
    uint8_t  if_num;
    uint16_t len;
    uint16_t offset;
    uint16_t checksum;
    uint16_t seq_num;
    uint8_t  flags;
} __attribute__((packed));

// 接口类型
#define ESP_STA_IF          0x00
#define ESP_AP_IF           0x01
#define ESP_SERIAL_IF       0x02
#define ESP_HCI_IF          0x03

// DMA对齐宏
#define SPI_DMA_ALIGN       4
#define IS_SPI_DMA_ALIGNED(len) (((len) & (SPI_DMA_ALIGN - 1)) == 0)
#define MAKE_SPI_DMA_ALIGNED(len) (((len) + (SPI_DMA_ALIGN - 1)) & ~(SPI_DMA_ALIGN - 1))

// 计算校验和（前向声明）
static uint16_t compute_checksum(uint8_t *data, uint16_t len)
{
    uint16_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// 缓冲区池
typedef struct {
    uint8_t buffer[SPI_BUFFER_SIZE];
    bool in_use;
} spi_buffer_pool_t;

static spi_buffer_pool_t tx_buffer_pool[SPI_BUFFER_POOL_SIZE];
static spi_buffer_pool_t rx_buffer_pool[SPI_BUFFER_POOL_SIZE];
static SemaphoreHandle_t buffer_pool_mutex = NULL;

// 多优先级队列
typedef struct {
    uint8_t *buffer;
    uint16_t len;
    uint8_t prio;
} spi_buffer_handle_t;

static QueueHandle_t tx_queues[MAX_PRIORITY_QUEUES];
static QueueHandle_t rx_queues[MAX_PRIORITY_QUEUES];

// 同步信号量
static SemaphoreHandle_t spi_sem = NULL;
static SemaphoreHandle_t spi_mutex = NULL;

// 前向声明
static void data_path_control(bool open);
static void esp_tx_pause(void);
static void esp_tx_resume(void);

// ==================== 中断处理函数 ====================

/**
 * 数据就绪中断处理函数 (IRAM_ATTR表示放在IRAM中，快速执行)
 * 当从机有数据要发送时触发
 */
static void IRAM_ATTR data_ready_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // 释放信号量，通知处理任务
    xSemaphoreGiveFromISR(spi_sem, &xHigherPriorityTaskWoken);
    
    // 如果需要，进行上下文切换
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * 握手信号中断处理函数
 * 当从机准备好接收数据时触发
 */
static void IRAM_ATTR handshake_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    xSemaphoreGiveFromISR(spi_sem, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ==================== GPIO初始化（中断驱动）====================

static void gpio_init_interrupt_driven(void)
{
    gpio_config_t io_conf;
    
    // 1. 配置CS引脚（输出）
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << SPI_CS_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    gpio_set_level(SPI_CS_PIN, 1);  // 初始高电平
    
    // 2. 配置握手信号引脚（输入，上升沿中断）
    io_conf.intr_type = GPIO_INTR_POSEDGE;  // 上升沿触发
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << SPI_HANDSHAKE_PIN);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    
    // 3. 配置数据就绪引脚（输入，上升沿中断）
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.pin_bit_mask = (1ULL << SPI_DATA_READY_PIN);
    gpio_config(&io_conf);
    
    // 4. 安装GPIO中断服务
    gpio_install_isr_service(0);
    
    // 5. 注册中断处理函数
    gpio_isr_handler_add(SPI_DATA_READY_PIN, data_ready_isr_handler, NULL);
    gpio_isr_handler_add(SPI_HANDSHAKE_PIN, handshake_isr_handler, NULL);
    
    ESP_LOGI(TAG, "GPIO initialized with interrupts:");
    ESP_LOGI(TAG, "  CS=%d, HS=%d (IRQ), DR=%d (IRQ)", 
             SPI_CS_PIN, SPI_HANDSHAKE_PIN, SPI_DATA_READY_PIN);
}

// ==================== 内存池管理 ====================

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

// ==================== 数据路径控制 ====================

static void data_path_control(bool open)
{
    if (open) {
        ESP_LOGI(TAG, "Opening data path...");
        
        // 清空所有队列
        for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
            xQueueReset(tx_queues[i]);
            xQueueReset(rx_queues[i]);
        }
        
        // 重置流控状态
        tx_pending = 0;
        tx_paused = false;
        
        // 等待硬件就绪
        vTaskDelay(pdMS_TO_TICKS(200));
        
        data_path_open = true;
        ESP_LOGI(TAG, "Data path opened");
        
    } else {
        ESP_LOGI(TAG, "Closing data path...");
        data_path_open = false;
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_LOGI(TAG, "Data path closed");
    }
}

// ==================== 流控机制 ====================

static void esp_tx_pause(void)
{
    if (!tx_paused) {
        tx_paused = true;
        ESP_LOGW(TAG, "TX paused (pending=%lu)", tx_pending);
    }
}

static void esp_tx_resume(void)
{
    if (tx_paused) {
        tx_paused = false;
        xSemaphoreGive(tx_flow_sem);
        ESP_LOGI(TAG, "TX resumed");
    }
}

// ==================== 多优先级队列操作 ====================

static int enqueue_tx_buffer(uint8_t *buffer, uint16_t len, uint8_t prio)
{
    if (prio >= MAX_PRIORITY_QUEUES) {
        prio = PRIO_Q_NORMAL;
    }
    
    // 流控检查
    if (tx_pending >= TX_MAX_PENDING && prio == PRIO_Q_NORMAL) {
        esp_tx_pause();
        return -EBUSY;
    }
    
    spi_buffer_handle_t handle = {
        .buffer = buffer,
        .len = len,
        .prio = prio
    };
    
    if (xQueueSend(tx_queues[prio], &handle, pdMS_TO_TICKS(10)) != pdTRUE) {
        return -ENOMEM;
    }
    
    if (prio == PRIO_Q_NORMAL) {
        tx_pending++;
    }
    
    // 通知SPI处理任务
    xSemaphoreGive(spi_sem);
    
    return 0;
}

static int dequeue_tx_buffer(spi_buffer_handle_t *handle)
{
    // 按优先级从高到低检查队列
    for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
        if (xQueueReceive(tx_queues[i], handle, 0) == pdTRUE) {
            if (i == PRIO_Q_NORMAL) {
                tx_pending--;
                
                // 检查是否需要恢复发送
                if (tx_paused && tx_pending < TX_RESUME_THRESHOLD) {
                    esp_tx_resume();
                }
            }
            return 0;
        }
    }
    return -ENOENT;
}

// ==================== SPI传输 ====================

#ifdef CONFIG_IDF_TARGET_ESP32
    #define SPI_HOST    HSPI_HOST
#else
    #define SPI_HOST    SPI2_HOST
#endif

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
    
    esp_err_t ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %d", ret);
        return ret;
    }
    
    spi_device_interface_config_t devcfg = {
        .mode = 2,
        .clock_speed_hz = SPI_CLK_MHZ * 1000 * 1000,
        .spics_io_num = -1,  // 软件控制CS
        .queue_size = SPI_QUEUE_SIZE,
    };
    
    ret = spi_bus_add_device(SPI_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %d", ret);
        return ret;
    }
    
    ESP_LOGI(TAG, "SPI Master initialized on HOST %d", SPI_HOST);
    return ESP_OK;
}

// ==================== 核心SPI处理线程 ====================

static void spi_processing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SPI processing task started (interrupt-driven)");
    
    uint8_t *tx_buf = NULL;
    uint8_t *rx_buf = NULL;
    spi_buffer_handle_t tx_handle = {0};
    
    while (1) {
        // 等待中断信号（阻塞，低功耗）
        if (xSemaphoreTake(spi_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            // 超时，打印心跳日志
            ESP_LOGD(TAG, "SPI task alive, waiting for interrupts...");
            continue;
        }
        
        stats.interrupts_count++;
        
        // 检查数据路径状态
        if (!data_path_open) {
            continue;
        }
        
        // 获取互斥锁
        if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        
        // 检查从机状态
        if (gpio_get_level(SPI_HANDSHAKE_PIN) == 0) {
            xSemaphoreGive(spi_mutex);
            continue;
        }
        
        // 获取发送数据（按优先级）
        bool has_tx_data = (dequeue_tx_buffer(&tx_handle) == 0);
        
        // 检查是否有接收数据
        bool has_rx_data = (gpio_get_level(SPI_DATA_READY_PIN) == 1);
        
        if (!has_tx_data && !has_rx_data) {
            xSemaphoreGive(spi_mutex);
            continue;
        }
        
        // 分配缓冲区
        tx_buf = alloc_tx_buffer();
        rx_buf = alloc_tx_buffer();
        if (!tx_buf || !rx_buf) {
            ESP_LOGW(TAG, "Buffer allocation failed");
            if (tx_buf) free_tx_buffer(tx_buf);
            if (rx_buf) free_tx_buffer(rx_buf);
            xSemaphoreGive(spi_mutex);
            continue;
        }
        
        // 准备发送数据
        if (has_tx_data) {
            memcpy(tx_buf, tx_handle.buffer, tx_handle.len);
            free_tx_buffer(tx_handle.buffer);
        } else {
            // 发送空包触发接收
            memset(tx_buf, 0, sizeof(struct esp_payload_header));
            struct esp_payload_header *hdr = (struct esp_payload_header *)tx_buf;
            hdr->if_type = 0x0F;
            hdr->len = 0;
        }
        
        // 拉低CS
        gpio_set_level(SPI_CS_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // 执行SPI传输
        uint16_t tx_len = SPI_BUFFER_SIZE;
        if (has_tx_data) {
            tx_len = tx_handle.len;
            if (!IS_SPI_DMA_ALIGNED(tx_len)) {
                tx_len = MAKE_SPI_DMA_ALIGNED(tx_len);
            }
        }
        
        spi_transaction_t trans = {
            .length = tx_len * 8,
            .tx_buffer = tx_buf,
            .rx_buffer = rx_buf,
        };
        
        esp_err_t ret = spi_device_transmit(spi_handle, &trans);
        
        // 拉高CS
        gpio_set_level(SPI_CS_PIN, 1);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit failed: %d", ret);
            stats.spi_trans_errors++;
        } else {
            // 处理接收数据
            struct esp_payload_header *rx_hdr = (struct esp_payload_header *)rx_buf;
            uint16_t rx_len = le16toh(rx_hdr->len);
            
            if (rx_hdr->if_type != 0x0F && rx_len > 0) {
                stats.rx_packets++;
                stats.rx_bytes += rx_len;
                
                // 放入接收队列
                spi_buffer_handle_t rx_handle = {
                    .buffer = rx_buf,
                    .len = rx_len,
                    .prio = (rx_hdr->if_type == ESP_SERIAL_IF) ? PRIO_Q_HIGH : PRIO_Q_NORMAL
                };
                
                uint8_t rx_prio = (rx_hdr->if_type == ESP_SERIAL_IF) ? PRIO_Q_HIGH : 
                                  (rx_hdr->if_type == ESP_HCI_IF) ? PRIO_Q_HIGH : PRIO_Q_NORMAL;
                
                if (xQueueSend(rx_queues[rx_prio], &rx_handle, pdMS_TO_TICKS(10)) != pdTRUE) {
                    free_tx_buffer(rx_buf);
                    stats.queue_full_count++;
                }
                
                rx_buf = NULL;  // 已移交所有权
            }
            
            if (has_tx_data) {
                stats.tx_packets++;
                stats.tx_bytes += tx_handle.len;
            }
        }
        
        // 释放缓冲区
        if (tx_buf) free_tx_buffer(tx_buf);
        if (rx_buf) free_tx_buffer(rx_buf);
        
        xSemaphoreGive(spi_mutex);
    }
}

// ==================== 应用层接口 ====================

int spi_host_send(uint8_t *data, uint16_t len, uint8_t prio)
{
    if (!data_path_open) {
        return -EPERM;
    }
    
    if (tx_paused && prio == PRIO_Q_NORMAL) {
        // 等待流控恢复
        if (xSemaphoreTake(tx_flow_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return -EBUSY;
        }
    }
    
    uint8_t *buf = alloc_tx_buffer();
    if (!buf) {
        return -ENOMEM;
    }
    
    // 封装ESP Header
    struct esp_payload_header *hdr = (struct esp_payload_header *)buf;
    hdr->if_type = ESP_STA_IF;
    hdr->if_num = 0;
    hdr->len = htole16(len);
    hdr->offset = htole16(sizeof(struct esp_payload_header));
    hdr->seq_num = htole16(sequence_num++);
    hdr->flags = 0;
    hdr->checksum = 0;
    
    memcpy(buf + sizeof(struct esp_payload_header), data, len);
    
    uint16_t total_len = sizeof(struct esp_payload_header) + len;
    hdr->checksum = htole16(compute_checksum(buf, total_len));
    
    if (!IS_SPI_DMA_ALIGNED(total_len)) {
        total_len = MAKE_SPI_DMA_ALIGNED(total_len);
    }
    
    int ret = enqueue_tx_buffer(buf, total_len, prio);
    if (ret < 0) {
        free_tx_buffer(buf);
    }
    
    return ret;
}

// ==================== 初始化 ====================

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SPI Host - Interrupt-Driven Version");
    ESP_LOGI(TAG, "============================================");
    
    // 创建同步原语
    spi_sem = xSemaphoreCreateBinary();
    spi_mutex = xSemaphoreCreateMutex();
    buffer_pool_mutex = xSemaphoreCreateMutex();
    tx_flow_sem = xSemaphoreCreateBinary();
    
    // 创建多优先级队列
    for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
        tx_queues[i] = xQueueCreate(SPI_QUEUE_SIZE, sizeof(spi_buffer_handle_t));
        rx_queues[i] = xQueueCreate(SPI_QUEUE_SIZE, sizeof(spi_buffer_handle_t));
    }
    
    // 初始化SPI
    ESP_ERROR_CHECK(spi_master_init());
    
    // 初始化GPIO（中断驱动）
    gpio_init_interrupt_driven();
    
    // 创建SPI处理任务
    xTaskCreate(spi_processing_task, "spi_proc", 4096, NULL, 5, NULL);
    
    // 打开数据路径
    data_path_control(true);
    
    ESP_LOGI(TAG, "Initialization complete. Waiting for interrupts...");
    
    // 主循环（低占用）
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        // 打印统计
        ESP_LOGI(TAG, "Stats: TX=%lu, RX=%lu, IRQ=%lu, Pending=%lu, Paused=%s",
                 stats.tx_packets, stats.rx_packets, stats.interrupts_count,
                 tx_pending, tx_paused ? "YES" : "NO");
    }
}
