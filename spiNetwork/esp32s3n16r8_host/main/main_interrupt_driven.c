/**
 * ESP32-WROOM-1 SPI Host - 中断驱动优化版本
 * 
 * 基于官方Linux驱动实现的中断驱动架构
 * 大幅降低CPU占用，提高响应速度
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "lwip/lwip_napt.h"
#include <endian.h>
#include "spi_config.h"
#include "esp_hosted_spi_proto.h"
#include "esp_http_client.h"
#include "esp_event.h"
#include "esp_rom_sys.h"
#include "lwip/esp_netif_net_stack.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

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

// 网络接口（SPI 收包在独立任务里 esp_netif_receive，避免与 spi_mutex 死锁）
static esp_netif_t *eth_netif = NULL;

typedef struct {
    uint8_t *payload;
    uint16_t len;
} eth_rx_msg_t;

static QueueHandle_t s_eth_rxq;

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
    uint32_t skip_hs_low;
    uint32_t rx_dummy;
} stats = {0};

static uint16_t sequence_num = 0;

// 数据路径控制
static volatile bool data_path_open = false;

// 流控
static volatile bool tx_paused = false;
static volatile uint32_t tx_pending = 0;
static SemaphoreHandle_t tx_flow_sem = NULL;

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

/* 与从机 process_spi_rx / network_adapter 判定一致；dummy(if_type==0xF,len==0) 返回 false */
static bool hosted_rx_frame_valid(uint8_t *rx_buf, uint16_t *out_payload_len)
{
    struct esp_payload_header *h = (struct esp_payload_header *)rx_buf;
    uint16_t len = le16toh(h->len);
    uint16_t offset = le16toh(h->offset);

    if (h->if_type == ESP_HOSTED_DUMMY_IF_TYPE || len == 0) {
        return false;
    }
    if (h->if_type != ESP_STA_IF && h->if_type != ESP_AP_IF && h->if_type != ESP_SERIAL_IF &&
        h->if_type != ESP_HCI_IF) {
        return false;
    }
    if (offset < sizeof(struct esp_payload_header) || (uint32_t)offset + (uint32_t)len > SPI_BUFFER_SIZE) {
        return false;
    }
    /* 从机默认 CONFIG_ESP_SPI_CHECKSUM=n 时 header->checksum 恒为 0，不做验证（见 spi_slave_api process_spi_rx） */
    uint16_t rx_checksum = le16toh(h->checksum);
    if (rx_checksum != 0) {
        uint16_t stor = h->checksum;
        h->checksum = 0;
        uint16_t calc = compute_checksum(rx_buf, offset + len);
        h->checksum = stor;
        if (calc != rx_checksum) {
            stats.checksum_errors++;
            return false;
        }
    }
    *out_payload_len = len;
    return true;
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
        ESP_LOGW(TAG, "TX paused (pending=%lu)", (unsigned long)tx_pending);
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
        
        /* Linux esp_spi.c：仅在 trans_ready(HS) 时交换；IRQ 略早于 HS 建立时轮询稍等 */
        for (int w = 0; w < 800 && gpio_get_level(SPI_HANDSHAKE_PIN) == 0; w++) {
            esp_rom_delay_us(10);
        }
        if (gpio_get_level(SPI_HANDSHAKE_PIN) == 0) {
            stats.skip_hs_low++;
            xSemaphoreGive(spi_mutex);
            continue;
        }
        
        /* 从机 DR 可能为短脉冲：边沿 IRQ 到达任务时 DR 常为 0。只要 HS=1 即应完成一帧
         * 交换（无 TX 时用 dummy），否则从机已 queue 的事务永远不会被主机时钟移出。 */
        bool has_tx_data = (dequeue_tx_buffer(&tx_handle) == 0);

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
            /* 与从机 get_next_tx_buffer() dummy 一致：if_type/if_num 各 4bit 置 0xF */
            memset(tx_buf, 0, sizeof(struct esp_payload_header));
            struct esp_payload_header *hdr = (struct esp_payload_header *)tx_buf;
            hdr->if_type = ESP_HOSTED_DUMMY_IF_TYPE;
            hdr->if_num = ESP_HOSTED_DUMMY_IF_TYPE;
            hdr->len = 0;
        }
        
        // 拉低CS
        gpio_set_level(SPI_CS_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        
        /* 从机 spi_slave 队列长度固定为 SPI_BUFFER_SIZE；主机必须始终交换整帧字节数 */
        spi_transaction_t trans = {
            .length = SPI_BUFFER_SIZE * 8,
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
            struct esp_payload_header *rx_hdr = (struct esp_payload_header *)rx_buf;
            uint16_t rx_plen = 0;

            if (rx_hdr->if_type == ESP_HOSTED_DUMMY_IF_TYPE || le16toh(rx_hdr->len) == 0) {
                stats.rx_dummy++;
            } else if (!hosted_rx_frame_valid(rx_buf, &rx_plen)) {
                stats.invalid_packets++;
                static unsigned s_bad_rx;
                if (s_bad_rx < 6) {
                    ESP_LOGW(TAG, "RX drop: if_type=%u len=%u off=%u (hdr_sz=%u buf=%u)",
                             (unsigned)rx_hdr->if_type, (unsigned)le16toh(rx_hdr->len),
                             (unsigned)le16toh(rx_hdr->offset), (unsigned)sizeof(struct esp_payload_header),
                             (unsigned)SPI_BUFFER_SIZE);
                    ESP_LOG_BUFFER_HEXDUMP(TAG, rx_buf, 24, ESP_LOG_WARN);
                    s_bad_rx++;
                }
            } else {
                stats.rx_packets++;
                stats.rx_bytes += rx_plen;

                uint16_t off = le16toh(rx_hdr->offset);
                if (s_eth_rxq && eth_netif &&
                    (rx_hdr->if_type == ESP_STA_IF || rx_hdr->if_type == ESP_AP_IF)) {
                    uint8_t *copy = (uint8_t *)malloc(rx_plen);
                    if (!copy) {
                        stats.queue_full_count++;
                    } else {
                        memcpy(copy, rx_buf + off, rx_plen);
                        eth_rx_msg_t m = {.payload = copy, .len = rx_plen};
                        if (xQueueSend(s_eth_rxq, &m, pdMS_TO_TICKS(20)) != pdTRUE) {
                            free(copy);
                            stats.queue_full_count++;
                        }
                    }
                    free_tx_buffer(rx_buf);
                    rx_buf = NULL;
                } else {
                    spi_buffer_handle_t rx_handle = {
                        .buffer = rx_buf,
                        .len = rx_plen,
                        .prio = (rx_hdr->if_type == ESP_SERIAL_IF) ? PRIO_Q_HIGH : PRIO_Q_NORMAL,
                    };

                    uint8_t rx_prio = (rx_hdr->if_type == ESP_SERIAL_IF)   ? PRIO_Q_HIGH
                                     : (rx_hdr->if_type == ESP_HCI_IF)    ? PRIO_Q_HIGH
                                                                          : PRIO_Q_NORMAL;

                    if (xQueueSend(rx_queues[rx_prio], &rx_handle, pdMS_TO_TICKS(10)) != pdTRUE) {
                        free_tx_buffer(rx_buf);
                        stats.queue_full_count++;
                    }

                    rx_buf = NULL;
                }
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
    
    struct esp_payload_header *hdr = (struct esp_payload_header *)buf;
    memset(hdr, 0, sizeof(*hdr));
    hdr->if_type = ESP_STA_IF;
    hdr->if_num = 0;
    hdr->len = htole16(len);
    hdr->offset = htole16(sizeof(struct esp_payload_header));
    hdr->seq_num = htole16(sequence_num++);
    hdr->flags = 0;
    hdr->checksum = 0;
    
    memcpy(buf + sizeof(struct esp_payload_header), data, len);

    uint16_t total_len = sizeof(struct esp_payload_header) + len;

    if (!IS_SPI_DMA_ALIGNED(total_len)) {
        total_len = MAKE_SPI_DMA_ALIGNED(total_len);
    }
    
    int ret = enqueue_tx_buffer(buf, total_len, prio);
    if (ret < 0) {
        free_tx_buffer(buf);
    }
    
    return ret;
}

/* 与 IDF examples/network/sta2eth 相同：自定义 driver.transmit + esp_netif_receive */
static esp_netif_ip_info_t s_spi_ip;

static esp_err_t spi_eth_transmit(void *h, void *buffer, size_t len)
{
    (void)h;
    if (len == 0 || len > (SPI_BUFFER_SIZE - sizeof(struct esp_payload_header))) {
        return ESP_ERR_INVALID_SIZE;
    }
    int r = spi_host_send((uint8_t *)buffer, (uint16_t)len, PRIO_Q_NORMAL);
    return (r == 0) ? ESP_OK : ESP_FAIL;
}

static void spi_eth_free_rx(void *h, void *buffer)
{
    (void)h;
    free(buffer);
}

static void eth_rx_dispatch_task(void *arg)
{
    (void)arg;
    eth_rx_msg_t m;
    for (;;) {
        if (xQueueReceive(s_eth_rxq, &m, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (eth_netif && m.payload) {
            esp_err_t e = esp_netif_receive(eth_netif, m.payload, m.len, NULL);
            if (e != ESP_OK) {
                free(m.payload);
            }
        } else if (m.payload) {
            free(m.payload);
        }
    }
}

// ==================== 网络与HTTP测试 ====================

static esp_err_t init_network_interface(void)
{
    memset(&s_spi_ip, 0, sizeof(s_spi_ip));
    s_spi_ip.ip.addr = ESP_IP4TOADDR(192, 168, 4, 2);
    s_spi_ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    s_spi_ip.gw.addr = ESP_IP4TOADDR(192, 168, 4, 1);

    esp_netif_inherent_config_t base = {
        .flags = (esp_netif_flags_t)(ESP_NETIF_FLAG_GARP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED | ESP_NETIF_FLAG_AUTOUP),
        .mac = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
        .ip_info = &s_spi_ip,
        .get_ip_event = 0,
        .lost_ip_event = 0,
        .if_key = "spi_br",
        .if_desc = "spi host bridge",
        .route_prio = 50,
        .bridge_info = NULL,
    };

    esp_netif_driver_ifconfig_t driver = {
        .handle = (void *)1,
        .transmit = spi_eth_transmit,
        .transmit_wrap = NULL,
        .driver_free_rx_buffer = spi_eth_free_rx,
    };

    struct esp_netif_netstack_config lwip_stack = {
        .lwip =
            {
                .init_fn = ethernetif_init,
                .input_fn = ethernetif_input,
            },
    };

    esp_netif_config_t cfg = {
        .base = &base,
        .driver = &driver,
        .stack = &lwip_stack,
    };

    eth_netif = esp_netif_new(&cfg);
    if (!eth_netif) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &s_spi_ip));

    esp_netif_dns_info_t dns_main = {0};
    dns_main.ip.type = IPADDR_TYPE_V4;
    dns_main.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns_main);

    esp_netif_dns_info_t dns_bak = {0};
    dns_bak.ip.type = IPADDR_TYPE_V4;
    dns_bak.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
    esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_BACKUP, &dns_bak);

    esp_netif_dns_info_t dns_fb = {0};
    dns_fb.ip.type = IPADDR_TYPE_V4;
    dns_fb.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
    esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_FALLBACK, &dns_fb);

    esp_netif_set_default_netif(eth_netif);
    esp_netif_action_start(eth_netif, NULL, 0, NULL);

    ESP_LOGI(TAG,
             "SPI bridge netif: 192.168.4.2 gw 192.168.4.1 | DNS main=192.168.4.1 backup=8.8.8.8 fallback=1.1.1.1");
    return ESP_OK;
}

static void ping_on_success(esp_ping_handle_t hdl, void *args)
{
    (void)args;
    uint16_t seqno = 0;
    uint32_t elapsed = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    ESP_LOGI(TAG, "ping 8.8.8.8 reply: icmp_seq=%u time=%" PRIu32 " ms", (unsigned)seqno, elapsed);
}

static void ping_on_timeout(esp_ping_handle_t hdl, void *args)
{
    (void)args;
    uint16_t seqno = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    ESP_LOGW(TAG, "ping 8.8.8.8: icmp_seq=%u timeout", (unsigned)seqno);
}

static void ping_on_end(esp_ping_handle_t hdl, void *args)
{
    SemaphoreHandle_t done = (SemaphoreHandle_t)args;
    uint32_t tx = 0, rx = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &tx, sizeof(tx));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
    ESP_LOGI(TAG, "ping 8.8.8.8 finished: %" PRIu32 " sent, %" PRIu32 " received", tx, rx);
    esp_ping_delete_session(hdl);
    if (done) {
        xSemaphoreGive(done);
    }
}

static void run_ping_8_8_8_8(void)
{
    ESP_LOGI(TAG, "========== ICMP ping 8.8.8.8 (5 probes) ==========");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) {
        ESP_LOGE(TAG, "ping: no sem");
        return;
    }

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 5;
    cfg.interval_ms = 800;
    cfg.timeout_ms = 3000;
    ip_addr_set_ip4_u32_val(cfg.target_addr, esp_ip4addr_aton("8.8.8.8"));

    esp_ping_callbacks_t cbs = {
        .cb_args = done,
        .on_ping_success = ping_on_success,
        .on_ping_timeout = ping_on_timeout,
        .on_ping_end = ping_on_end,
    };

    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &ping);
    if (err != ESP_OK || !ping) {
        ESP_LOGE(TAG, "esp_ping_new_session: %s", esp_err_to_name(err));
        vSemaphoreDelete(done);
        return;
    }

    err = esp_ping_start(ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ping_start: %s", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        vSemaphoreDelete(done);
        return;
    }

    if (xSemaphoreTake(done, pdMS_TO_TICKS(25000)) != pdTRUE) {
        ESP_LOGW(TAG, "ping 8.8.8.8: wait end timeout, stopping session");
        esp_ping_stop(ping);
        (void)xSemaphoreTake(done, pdMS_TO_TICKS(3000));
    }

    vSemaphoreDelete(done);
    ESP_LOGI(TAG, "========== ICMP ping end ==========");
}

// HTTP测试任务 - 访问百度
static void http_test_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(10000));  // 等待网络就绪

    run_ping_8_8_8_8();

    ESP_LOGI(TAG, "========== HTTP测试开始 ==========");
    {
        struct addrinfo hints = {0};
        struct addrinfo *res = NULL;
        hints.ai_family = AF_INET;
        int gai = getaddrinfo("www.baidu.com", NULL, &hints, &res);
        if (gai != 0 || res == NULL) {
            ESP_LOGW(TAG, "getaddrinfo(www.baidu.com) failed ret=%d (DNS issue; ping 8.8.8.8 does not use DNS)", gai);
        } else {
            const struct sockaddr_in *sa = (const struct sockaddr_in *)res->ai_addr;
            ESP_LOGI(TAG, "getaddrinfo(www.baidu.com) OK -> %s", inet_ntoa(sa->sin_addr));
            freeaddrinfo(res);
        }
    }

    ESP_LOGI(TAG, "正在访问 http://www.baidu.com ...");

    esp_http_client_config_t config = {
        .url = "http://www.baidu.com",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP客户端初始化失败");
        vTaskDelete(NULL);
        return;
    }
    
    // 设置请求头
    esp_http_client_set_header(client, "User-Agent", "ESP32-HTTP-Client/1.0");
    esp_http_client_set_header(client, "Accept", "text/html,application/xhtml+xml");
    
    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP请求成功!");
        ESP_LOGI(TAG, "状态码: %d", status_code);
        ESP_LOGI(TAG, "内容长度: %d bytes", content_length);
        
        // 读取并打印响应内容（前512字节）
        char response_buffer[513] = {0};
        int read_len = esp_http_client_read_response(client, response_buffer, sizeof(response_buffer)-1);
        if (read_len > 0) {
            response_buffer[read_len] = '\0';
            ESP_LOGI(TAG, "响应内容(前%d字节):\n%s", read_len, response_buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "========== HTTP测试结束 ==========");
    
    // 完成后删除任务
    vTaskDelete(NULL);
}

// ==================== 初始化 ====================

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SPI Host - Interrupt-Driven Version");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "SPI proto: hdr=%u bytes, frame buf=%u bytes (must match router slave 1600)",
             (unsigned)sizeof(struct esp_payload_header), (unsigned)SPI_BUFFER_SIZE);
    ESP_LOGI(TAG, "Pins MOSI=%d MISO=%d CLK=%d CS=%d HS=%d DR=%d @ %d MHz", SPI_MOSI_PIN, SPI_MISO_PIN,
             SPI_CLK_PIN, SPI_CS_PIN, SPI_HANDSHAKE_PIN, SPI_DATA_READY_PIN, SPI_CLK_MHZ);
    
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

    s_eth_rxq = xQueueCreate(24, sizeof(eth_rx_msg_t));
    if (!s_eth_rxq) {
        ESP_LOGE(TAG, "Failed to create eth RX queue");
        return;
    }

    ESP_LOGI(TAG, "Initializing network stack (before SPI, eth_netif must exist for RX path)...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_network_interface());
    xTaskCreate(eth_rx_dispatch_task, "eth_rx", 4096, NULL, 6, NULL);

    // 初始化SPI
    ESP_ERROR_CHECK(spi_master_init());

    // 初始化GPIO（中断驱动）
    gpio_init_interrupt_driven();

    // 创建SPI处理任务
    xTaskCreate(spi_processing_task, "spi_proc", 4096, NULL, 5, NULL);

    // 打开数据路径
    data_path_control(true);
    
    // 创建HTTP测试任务
    xTaskCreate(http_test_task, "http_test", 8192, NULL, 4, NULL);
    ESP_LOGI(TAG, "http_test task: after 10s delay -> ping 8.8.8.8 -> http://www.baidu.com");
    
    ESP_LOGI(TAG, "Initialization complete. Waiting for interrupts...");
    
    // 主循环（低占用）
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        // 打印统计
        ESP_LOGI(TAG,
                 "Stats: TX=%lu RX=%lu IRQ=%lu Pend=%lu Pause=%s | skipHS=%lu dummyRX=%lu inv=%lu csum=%lu spiErr=%lu",
                 (unsigned long)stats.tx_packets, (unsigned long)stats.rx_packets,
                 (unsigned long)stats.interrupts_count, (unsigned long)tx_pending,
                 tx_paused ? "YES" : "NO", (unsigned long)stats.skip_hs_low,
                 (unsigned long)stats.rx_dummy, (unsigned long)stats.invalid_packets,
                 (unsigned long)stats.checksum_errors, (unsigned long)stats.spi_trans_errors);
    }
}
