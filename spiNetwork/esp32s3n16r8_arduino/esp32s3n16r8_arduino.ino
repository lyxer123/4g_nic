/**
 * ESP32-S3 (N16R8) SPI Host — Arduino 版，与 esp32s3n16r8_espidf 中 main_interrupt_driven.c 对齐
 *
 * 要点（旧版 ino 无法上网的原因）：
 * 1) esp_payload_header 必须与 managed_components/.../wifi_dongle_adapter.h 一致（12 字节 packed）
 * 2) 每帧 SPI 必须交换 SPI_BUFFER_SIZE(1600) 字节，不能只传短包
 * 3) dummy 帧：if_type / if_num 均为 0x0F（与从机一致）
 * 4) 引脚与时钟须与路由器侧及 spi_config.h 一致
 * 5) 通过 esp_netif 自定义 transmit + esp_netif_receive 接入 lwIP 才能「上网」
 *
 * 开发板：Arduino IDE 中选 ESP32S3 Dev Module（或等价），勿选带「USB CDC On Boot」冲突的 USB 引脚与本 SPI 重叠。
 *
 * 串口监视器：若开启 USB CDC On Boot，应用层 Serial/ser() 走 USB 串口；ROM 启动日志仍在 UART0。
 * 请打开与「USB CDC」对应的 COM 口，或关闭 CDC 仅用 UART0，否则会只看到两段 ROM 日志。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>

#include <endian.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_rom_sys.h"
#include "esp_http_client.h"
#include "ping/ping_sock.h"

#include "lwip/esp_netif_net_stack.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"

/*
 * Arduino 会在本文件最前面（紧跟 include 之后）自动生成 static 函数的原型。
 * 若 typedef 写在后面，dequeue_tx_buffer(spi_host_tx_msg_t *) 的原型会早于类型定义 → 编译失败。
 */
typedef struct {
    uint8_t *buffer;
    uint16_t len;
    uint8_t prio;
} spi_host_tx_msg_t;

// ——— 与 spiNetwork/esp32s3n16r8_espidf/main/spi_config.h 保持一致 ———
#define SPI_MOSI_PIN         16
#define SPI_MISO_PIN         17
#define SPI_CLK_PIN          18
/* ESP-IDF 5.4 + Arduino C++：GPIO 驱动 API 使用 gpio_num_t，勿用裸 int */
#define SPI_CS_PIN           GPIO_NUM_8
#define SPI_HANDSHAKE_PIN    GPIO_NUM_3
#define SPI_DATA_READY_PIN   GPIO_NUM_46
#define SPI_CLK_MHZ          30
#define SPI_BUFFER_SIZE      1600
#define SPI_QUEUE_SIZE       20
#define SPI_BUFFER_POOL_SIZE 8

#define ESP_HOSTED_DUMMY_IF_TYPE 0x0F

struct __attribute__((packed)) esp_payload_header {
    uint8_t if_type : 4;
    uint8_t if_num : 4;
    uint8_t flags;
    uint16_t len;
    uint16_t offset;
    uint16_t checksum;
    uint16_t seq_num;
    uint8_t reserved2;
    union {
        uint8_t reserved3;
        uint8_t hci_pkt_type;
        uint8_t priv_pkt_type;
    };
};

static_assert(sizeof(struct esp_payload_header) == 12, "esp_payload_header must be 12 bytes");

#define MAX_PRIORITY_QUEUES   3
#define PRIO_Q_HIGH           0
#define PRIO_Q_NORMAL         1
#define PRIO_Q_LOW            2
#define TX_MAX_PENDING        100
#define TX_RESUME_THRESHOLD   20

#define ESP_STA_IF            0x00
#define ESP_AP_IF             0x01
#define ESP_SERIAL_IF         0x02
#define ESP_HCI_IF            0x03

#define SPI_DMA_ALIGN         4
#define IS_SPI_DMA_ALIGNED(len) (((len) & (SPI_DMA_ALIGN - 1)) == 0)
#define MAKE_SPI_DMA_ALIGNED(len) (((len) + (SPI_DMA_ALIGN - 1)) & ~(SPI_DMA_ALIGN - 1))

static const char *TAG = "spi_host_arduino";

/** 直接走 Arduino Serial，不依赖 ESP_LOG 默认级别 / USB-UART 绑定 */
static void ser(const char *line)
{
    Serial.println(line);
    Serial.flush();
}

static void serf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.println(buf);
    Serial.flush();
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "OTHER";
    }
}

static spi_device_handle_t s_spi = NULL;
static esp_netif_t *s_eth_netif = NULL;

typedef struct {
    uint8_t *payload;
    uint16_t len;
} eth_rx_msg_t;

static QueueHandle_t s_eth_rxq = NULL;

static struct {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t checksum_errors;
    uint32_t queue_full_count;
    uint32_t spi_trans_errors;
    uint32_t invalid_packets;
    uint32_t interrupts_count;
    uint32_t skip_hs_low;
    uint32_t rx_dummy;
} s_stats = {};

static uint16_t s_sequence_num = 0;
static volatile bool s_data_path_open = false;
static volatile bool s_tx_paused = false;
static volatile uint32_t s_tx_pending = 0;
static SemaphoreHandle_t s_tx_flow_sem = NULL;
static SemaphoreHandle_t s_spi_sem = NULL;
static SemaphoreHandle_t s_spi_mutex = NULL;
static SemaphoreHandle_t s_buffer_pool_mutex = NULL;

static QueueHandle_t s_tx_queues[MAX_PRIORITY_QUEUES] = {};

typedef struct {
    uint8_t buffer[SPI_BUFFER_SIZE];
    bool in_use;
} spi_buffer_pool_t;

static spi_buffer_pool_t s_tx_buffer_pool[SPI_BUFFER_POOL_SIZE];

static uint16_t compute_checksum(uint8_t *data, uint16_t len)
{
    uint16_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

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
    if (offset < sizeof(struct esp_payload_header) ||
        (uint32_t)offset + (uint32_t)len > SPI_BUFFER_SIZE) {
        return false;
    }
    uint16_t rx_checksum = le16toh(h->checksum);
    if (rx_checksum != 0) {
        uint16_t stor = h->checksum;
        h->checksum = 0;
        uint16_t calc = compute_checksum(rx_buf, offset + len);
        h->checksum = stor;
        if (calc != rx_checksum) {
            s_stats.checksum_errors++;
            return false;
        }
    }
    *out_payload_len = len;
    return true;
}

static void IRAM_ATTR data_ready_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_spi_sem, &hpw);
    if (hpw) {
        portYIELD_FROM_ISR();
    }
}

static void IRAM_ATTR handshake_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_spi_sem, &hpw);
    if (hpw) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t gpio_init_interrupt_driven(void)
{
    esp_err_t err;
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << SPI_CS_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(SPI_CS_PIN, 1);

    io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << SPI_HANDSHAKE_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    io_conf.pin_bit_mask = (1ULL << SPI_DATA_READY_PIN);
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    /* Arduino 核心可能已调用 gpio_install_isr_service；重复安装会失败 */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(SPI_DATA_READY_PIN, data_ready_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add DR: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_isr_handler_add(SPI_HANDSHAKE_PIN, handshake_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add HS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "GPIO IRQ: CS=%d HS=%d DR=%d", (int)SPI_CS_PIN, (int)SPI_HANDSHAKE_PIN,
             (int)SPI_DATA_READY_PIN);
    return ESP_OK;
}

static uint8_t *alloc_tx_buffer(void)
{
    xSemaphoreTake(s_buffer_pool_mutex, portMAX_DELAY);
    for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
        if (!s_tx_buffer_pool[i].in_use) {
            s_tx_buffer_pool[i].in_use = true;
            memset(s_tx_buffer_pool[i].buffer, 0, SPI_BUFFER_SIZE);
            xSemaphoreGive(s_buffer_pool_mutex);
            return s_tx_buffer_pool[i].buffer;
        }
    }
    xSemaphoreGive(s_buffer_pool_mutex);
    s_stats.queue_full_count++;
    return NULL;
}

static void free_tx_buffer(uint8_t *buf)
{
    if (!buf) {
        return;
    }
    xSemaphoreTake(s_buffer_pool_mutex, portMAX_DELAY);
    for (int i = 0; i < SPI_BUFFER_POOL_SIZE; i++) {
        if (s_tx_buffer_pool[i].buffer == buf) {
            s_tx_buffer_pool[i].in_use = false;
            break;
        }
    }
    xSemaphoreGive(s_buffer_pool_mutex);
}

static void data_path_control(bool open)
{
    if (open) {
        for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
            if (s_tx_queues[i]) {
                xQueueReset(s_tx_queues[i]);
            }
        }
        s_tx_pending = 0;
        s_tx_paused = false;
        vTaskDelay(pdMS_TO_TICKS(200));
        s_data_path_open = true;
        ESP_LOGI(TAG, "Data path opened");
        ser("[data_path] OPEN");
    } else {
        s_data_path_open = false;
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_LOGI(TAG, "Data path closed");
    }
}

static void esp_tx_pause(void)
{
    if (!s_tx_paused) {
        s_tx_paused = true;
        ESP_LOGW(TAG, "TX paused pending=%lu", (unsigned long)s_tx_pending);
    }
}

static void esp_tx_resume(void)
{
    if (s_tx_paused) {
        s_tx_paused = false;
        xSemaphoreGive(s_tx_flow_sem);
        ESP_LOGI(TAG, "TX resumed");
    }
}

static int enqueue_tx_buffer(uint8_t *buffer, uint16_t len, uint8_t prio)
{
    if (prio >= MAX_PRIORITY_QUEUES) {
        prio = PRIO_Q_NORMAL;
    }
    if (s_tx_pending >= TX_MAX_PENDING && prio == PRIO_Q_NORMAL) {
        esp_tx_pause();
        return -EBUSY;
    }
    spi_host_tx_msg_t handle = {.buffer = buffer, .len = len, .prio = prio};
    if (xQueueSend(s_tx_queues[prio], &handle, pdMS_TO_TICKS(10)) != pdTRUE) {
        return -ENOMEM;
    }
    if (prio == PRIO_Q_NORMAL) {
        s_tx_pending++;
    }
    xSemaphoreGive(s_spi_sem);
    return 0;
}

static int dequeue_tx_buffer(spi_host_tx_msg_t *out_msg)
{
    for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
        if (xQueueReceive(s_tx_queues[i], out_msg, 0) == pdTRUE) {
            if (i == PRIO_Q_NORMAL) {
                s_tx_pending--;
                if (s_tx_paused && s_tx_pending < TX_RESUME_THRESHOLD) {
                    esp_tx_resume();
                }
            }
            return 0;
        }
    }
    return -ENOENT;
}

#if CONFIG_IDF_TARGET_ESP32
#define SPI_HOST HSPI_HOST
#else
#define SPI_HOST SPI2_HOST
#endif

static esp_err_t spi_master_init_local(void)
{
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = SPI_MOSI_PIN;
    buscfg.miso_io_num = SPI_MISO_PIN;
    buscfg.sclk_io_num = SPI_CLK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SPI_BUFFER_SIZE;

    esp_err_t ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 2;
    devcfg.clock_speed_hz = SPI_CLK_MHZ * 1000 * 1000;
    devcfg.spics_io_num = -1;
    devcfg.queue_size = SPI_QUEUE_SIZE;

    ret = spi_bus_add_device(SPI_HOST, &devcfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI host %d MHz mode 2 (SPI_HOST enum=%d)", SPI_CLK_MHZ, (int)SPI_HOST);
    return ESP_OK;
}

static int spi_host_send(uint8_t *data, uint16_t len, uint8_t prio);

static void spi_processing_task(void *pvParameters)
{
    (void)pvParameters;
    static bool s_spi_proc_announced;
    if (!s_spi_proc_announced) {
        s_spi_proc_announced = true;
        ser("[spi_proc] task started (SPI exchanges when DR/HS IRQ fires)");
    }
    uint8_t *tx_buf = NULL;
    uint8_t *rx_buf = NULL;
    spi_host_tx_msg_t tx_handle = {};

    for (;;) {
        if (xSemaphoreTake(s_spi_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        s_stats.interrupts_count++;
        if (!s_data_path_open) {
            continue;
        }
        if (xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        for (int w = 0; w < 800 && gpio_get_level(SPI_HANDSHAKE_PIN) == 0; w++) {
            esp_rom_delay_us(10);
        }
        if (gpio_get_level(SPI_HANDSHAKE_PIN) == 0) {
            s_stats.skip_hs_low++;
            xSemaphoreGive(s_spi_mutex);
            continue;
        }

        bool has_tx_data = (dequeue_tx_buffer(&tx_handle) == 0);
        tx_buf = alloc_tx_buffer();
        rx_buf = alloc_tx_buffer();
        if (!tx_buf || !rx_buf) {
            if (tx_buf) {
                free_tx_buffer(tx_buf);
            }
            if (rx_buf) {
                free_tx_buffer(rx_buf);
            }
            if (has_tx_data && tx_handle.buffer) {
                free_tx_buffer(tx_handle.buffer);
            }
            xSemaphoreGive(s_spi_mutex);
            continue;
        }

        if (has_tx_data) {
            memcpy(tx_buf, tx_handle.buffer, tx_handle.len);
            free_tx_buffer(tx_handle.buffer);
            tx_handle.buffer = NULL;
        } else {
            memset(tx_buf, 0, sizeof(struct esp_payload_header));
            struct esp_payload_header *hdr = (struct esp_payload_header *)tx_buf;
            hdr->if_type = ESP_HOSTED_DUMMY_IF_TYPE;
            hdr->if_num = ESP_HOSTED_DUMMY_IF_TYPE;
            hdr->len = 0;
        }

        gpio_set_level(SPI_CS_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1));

        spi_transaction_t trans = {};
        trans.length = SPI_BUFFER_SIZE * 8;
        trans.tx_buffer = tx_buf;
        trans.rx_buffer = rx_buf;

        esp_err_t ret = spi_device_transmit(s_spi, &trans);
        gpio_set_level(SPI_CS_PIN, 1);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
            s_stats.spi_trans_errors++;
        } else {
            struct esp_payload_header *rx_hdr = (struct esp_payload_header *)rx_buf;
            uint16_t rx_plen = 0;

            if (rx_hdr->if_type == ESP_HOSTED_DUMMY_IF_TYPE || le16toh(rx_hdr->len) == 0) {
                s_stats.rx_dummy++;
            } else if (!hosted_rx_frame_valid(rx_buf, &rx_plen)) {
                s_stats.invalid_packets++;
            } else {
                s_stats.rx_packets++;
                s_stats.rx_bytes += rx_plen;
                uint16_t off = le16toh(rx_hdr->offset);
                if (s_eth_rxq && s_eth_netif &&
                    (rx_hdr->if_type == ESP_STA_IF || rx_hdr->if_type == ESP_AP_IF)) {
                    uint8_t *copy = (uint8_t *)malloc(rx_plen);
                    if (!copy) {
                        s_stats.queue_full_count++;
                    } else {
                        memcpy(copy, rx_buf + off, rx_plen);
                        eth_rx_msg_t m = {.payload = copy, .len = rx_plen};
                        if (xQueueSend(s_eth_rxq, &m, pdMS_TO_TICKS(20)) != pdTRUE) {
                            free(copy);
                            s_stats.queue_full_count++;
                        }
                    }
                    free_tx_buffer(rx_buf);
                    rx_buf = NULL;
                }
            }

            if (has_tx_data) {
                s_stats.tx_packets++;
                s_stats.tx_bytes += tx_handle.len;
            }
        }

        if (tx_buf) {
            free_tx_buffer(tx_buf);
        }
        if (rx_buf) {
            free_tx_buffer(rx_buf);
        }
        xSemaphoreGive(s_spi_mutex);
    }
}

static int spi_host_send(uint8_t *data, uint16_t len, uint8_t prio)
{
    if (!s_data_path_open) {
        return -EPERM;
    }
    if (s_tx_paused && prio == PRIO_Q_NORMAL) {
        if (xSemaphoreTake(s_tx_flow_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
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
    hdr->seq_num = htole16(s_sequence_num++);
    hdr->flags = 0;
    hdr->checksum = 0;
    memcpy(buf + sizeof(struct esp_payload_header), data, len);

    uint16_t total_len = sizeof(struct esp_payload_header) + len;
    if (!IS_SPI_DMA_ALIGNED(total_len)) {
        total_len = MAKE_SPI_DMA_ALIGNED(total_len);
    }
    int r = enqueue_tx_buffer(buf, total_len, prio);
    if (r < 0) {
        free_tx_buffer(buf);
    }
    return r;
}

static esp_netif_ip_info_t s_spi_ip;
static esp_netif_inherent_config_t s_inherent_cfg;
static esp_netif_driver_ifconfig_t s_driver_cfg;
static struct esp_netif_netstack_config s_netstack_cfg;

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
        if (s_eth_netif && m.payload) {
            esp_err_t e = esp_netif_receive(s_eth_netif, m.payload, m.len, NULL);
            if (e != ESP_OK) {
                free(m.payload);
            }
        } else if (m.payload) {
            free(m.payload);
        }
    }
}

static esp_err_t init_network_interface(void)
{
    ser("[netif] init_network_interface() ...");
    memset(&s_spi_ip, 0, sizeof(s_spi_ip));
    s_spi_ip.ip.addr = ESP_IP4TOADDR(192, 168, 4, 2);
    s_spi_ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    s_spi_ip.gw.addr = ESP_IP4TOADDR(192, 168, 4, 1);

    memset(&s_inherent_cfg, 0, sizeof(s_inherent_cfg));
    s_inherent_cfg.flags = (esp_netif_flags_t)(ESP_NETIF_FLAG_GARP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED |
                                               ESP_NETIF_FLAG_AUTOUP);
    s_inherent_cfg.mac[0] = 0x02;
    s_inherent_cfg.mac[1] = 0x03;
    s_inherent_cfg.mac[2] = 0x04;
    s_inherent_cfg.mac[3] = 0x05;
    s_inherent_cfg.mac[4] = 0x06;
    s_inherent_cfg.mac[5] = 0x07;
    s_inherent_cfg.ip_info = &s_spi_ip;
    s_inherent_cfg.if_key = "spi_br";
    s_inherent_cfg.if_desc = "spi host bridge";
    s_inherent_cfg.route_prio = 50;

    memset(&s_driver_cfg, 0, sizeof(s_driver_cfg));
    s_driver_cfg.handle = (void *)1;
    s_driver_cfg.transmit = spi_eth_transmit;
    s_driver_cfg.transmit_wrap = NULL;
    s_driver_cfg.driver_free_rx_buffer = spi_eth_free_rx;

    memset(&s_netstack_cfg, 0, sizeof(s_netstack_cfg));
    s_netstack_cfg.lwip.init_fn = ethernetif_init;
    s_netstack_cfg.lwip.input_fn = ethernetif_input;

    esp_netif_config_t cfg = {};
    cfg.base = &s_inherent_cfg;
    cfg.driver = &s_driver_cfg;
    cfg.stack = &s_netstack_cfg;

    s_eth_netif = esp_netif_new(&cfg);
    if (!s_eth_netif) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        ser("[netif] ERROR esp_netif_new failed");
        return ESP_FAIL;
    }
    ser("[netif] esp_netif_new OK");

    esp_err_t ne = esp_netif_set_ip_info(s_eth_netif, &s_spi_ip);
    if (ne != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_set_ip_info: %s", esp_err_to_name(ne));
        serf("[netif] ERROR esp_netif_set_ip_info %s", esp_err_to_name(ne));
        return ne;
    }

    /*
     * 主 DNS 不要用 192.168.4.1：很多 esp-iot-bridge 路由器只做转发、不在网关答 DNS，
     * getaddrinfo 会得到 lwIP EAI_FAIL(202)。公网已通时优先用 8.8.8.8 / 1.1.1.1。
     */
    esp_netif_dns_info_t dns_main = {};
    dns_main.ip.type = IPADDR_TYPE_V4;
    dns_main.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
    esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns_main);

    esp_netif_dns_info_t dns_bak = {};
    dns_bak.ip.type = IPADDR_TYPE_V4;
    dns_bak.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
    esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_BACKUP, &dns_bak);

    esp_netif_dns_info_t dns_fb = {};
    dns_fb.ip.type = IPADDR_TYPE_V4;
    dns_fb.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_FALLBACK, &dns_fb);

    esp_netif_set_default_netif(s_eth_netif);
    esp_netif_action_start(s_eth_netif, NULL, 0, NULL);

    /* 无 PHY 的自定义以太网：声明链路已连接，否则部分 lwIP 路径对「在线」判断异常 */
    esp_netif_action_connected(s_eth_netif, NULL, 0, NULL);

    /* esp_netif_set_dns_info 有时未同步到 lwIP 全局 DNS，再显式写入解析器 */
    ip_addr_t dns0, dns1;
    ip_addr_set_ip4_u32_val(dns0, esp_ip4addr_aton("8.8.8.8"));
    ip_addr_set_ip4_u32_val(dns1, esp_ip4addr_aton("1.1.1.1"));
    dns_setserver(0, &dns0);
    dns_setserver(1, &dns1);

    ESP_LOGI(TAG, "Netif 192.168.4.2 gw 192.168.4.1 | DNS main=8.8.8.8 backup=1.1.1.1 fb=192.168.4.1");
    serf("[netif] OK 192.168.4.2/24 link+lwIP DNS 8.8.8.8/1.1.1.1 heap=%u", (unsigned)esp_get_free_heap_size());
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
    serf("[ping] 8.8.8.8 seq=%u %" PRIu32 "ms", (unsigned)seqno, elapsed);
}

static void ping_on_timeout(esp_ping_handle_t hdl, void *args)
{
    (void)args;
    uint16_t seqno = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    ESP_LOGW(TAG, "ping 8.8.8.8: icmp_seq=%u timeout", (unsigned)seqno);
    serf("[ping] 8.8.8.8 seq=%u TIMEOUT", (unsigned)seqno);
}

static void ping_on_end(esp_ping_handle_t hdl, void *args)
{
    SemaphoreHandle_t done = (SemaphoreHandle_t)args;
    uint32_t tx = 0, rx = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &tx, sizeof(tx));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
    ESP_LOGI(TAG, "ping 8.8.8.8 finished: %" PRIu32 " sent, %" PRIu32 " received", tx, rx);
    serf("[ping] done sent=%" PRIu32 " recv=%" PRIu32, tx, rx);
    esp_ping_delete_session(hdl);
    if (done) {
        xSemaphoreGive(done);
    }
}

static void run_ping_8_8_8_8(void)
{
    ESP_LOGI(TAG, "========== ICMP ping 8.8.8.8 (5 probes) ==========");
    ser("========== ICMP ping 8.8.8.8 (5) ==========");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) {
        ESP_LOGE(TAG, "ping: semaphore create failed");
        return;
    }

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 5;
    cfg.interval_ms = 800;
    cfg.timeout_ms = 3000;
    ip_addr_set_ip4_u32_val(cfg.target_addr, esp_ip4addr_aton("8.8.8.8"));

    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = done;
    cbs.on_ping_success = ping_on_success;
    cbs.on_ping_timeout = ping_on_timeout;
    cbs.on_ping_end = ping_on_end;

    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &ping);
    if (err != ESP_OK || !ping) {
        ESP_LOGE(TAG, "esp_ping_new_session: %s", esp_err_to_name(err));
        serf("[ping] esp_ping_new_session FAIL %s", esp_err_to_name(err));
        vSemaphoreDelete(done);
        return;
    }

    err = esp_ping_start(ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ping_start: %s", esp_err_to_name(err));
        serf("[ping] esp_ping_start FAIL %s", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        vSemaphoreDelete(done);
        return;
    }

    if (xSemaphoreTake(done, pdMS_TO_TICKS(25000)) != pdTRUE) {
        ESP_LOGW(TAG, "ping 8.8.8.8: wait end timeout, stopping");
        esp_ping_stop(ping);
        (void)xSemaphoreTake(done, pdMS_TO_TICKS(3000));
    }

    vSemaphoreDelete(done);
    ESP_LOGI(TAG, "========== ICMP ping end ==========");
    ser("========== ICMP ping end ==========");
}

/** host_hdr 非 NULL 时设置 HTTP Host（用于按 IP 访问需虚拟主机的站点） */
static esp_err_t http_get_url(const char *label, const char *url, const char *host_hdr)
{
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 20000;
    config.buffer_size = 2048;
    /* 明文 HTTP：禁止走 TLS；禁止跟随 302 到 https（否则会再 getaddrinfo / esp-tls，DNS 坏时必挂） */
    config.transport_type = HTTP_TRANSPORT_OVER_TCP;
    config.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        serf("[%s] esp_http_client_init failed", label);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "User-Agent", "ESP32-Arduino-SPI-Host/1.0");
    if (host_hdr) {
        esp_http_client_set_header(client, "Host", host_hdr);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int code = esp_http_client_get_status_code(client);
        serf("[%s] HTTP OK status=%d", label, code);
        char buf[256];
        int n = esp_http_client_read_response(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            serf("[%s] body (%d): %.120s", label, n, buf);
        }
    } else {
        serf("[%s] HTTP FAIL %s", label, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

static void net_connectivity_test_task(void *pvParameters)
{
    (void)pvParameters;
    ser("[net_test] task started, delay 12s for link/SPI ...");
    vTaskDelay(pdMS_TO_TICKS(12000));
    serf("[net_test] delay done heap=%u", (unsigned)esp_get_free_heap_size());

    run_ping_8_8_8_8();

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "========== HTTP GET http://www.baidu.com ==========");
    ser("========== HTTP GET http://www.baidu.com ==========");
    ESP_LOGI(TAG, "DNS test: getaddrinfo(www.baidu.com) ...");

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            serf("[http] getaddrinfo retry %d/2 ...", attempt);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        res = NULL;
        gai = getaddrinfo("www.baidu.com", "80", &hints, &res);
        if (gai == 0 && res != NULL) {
            break;
        }
        if (res) {
            freeaddrinfo(res);
            res = NULL;
        }
    }

    if (gai != 0 || res == NULL) {
        ESP_LOGW(TAG, "getaddrinfo failed %d", gai);
        /* lwIP: 202 = EAI_FAIL（常见：网关不提供 DNS 或解析超时） */
        serf("[http] getaddrinfo FAIL ret=%d (202=lwIP EAI_FAIL if DNS error)", gai);
    } else {
        const struct sockaddr_in *sa = (const struct sockaddr_in *)res->ai_addr;
        ESP_LOGI(TAG, "www.baidu.com -> %s", inet_ntoa(sa->sin_addr));
        serf("[http] DNS OK www.baidu.com -> %s", inet_ntoa(sa->sin_addr));
        freeaddrinfo(res);
        res = NULL;
    }

    esp_err_t err = http_get_url("http_baidu", "http://www.baidu.com", NULL);
    if (err != ESP_OK) {
        /* 不依赖 DNS：直连网关测 TCP */
        ser("[http] baidu(hostname) fail -> try http://192.168.4.1/ (no DNS)");
        (void)http_get_url("http_gw", "http://192.168.4.1/", NULL);
    }

    /* 以下两项每次必跑（不依赖 DNS），便于对比国内 CDN 与 example.com */
    ser("---------- HTTP by IP: baidu CDN 110.242.68.66 + Host ----------");
    (void)http_get_url("http_baidu_ip", "http://110.242.68.66/", "www.baidu.com");

    ser("---------- HTTP by IP: example.com 93.184.216.34 + Host ----------");
    (void)http_get_url("http_example", "http://93.184.216.34/", "example.com");

    ESP_LOGI(TAG, "========== HTTP test end ==========");
    ser("========== HTTP test end ==========");
    vTaskDelete(NULL);
}

void setup()
{
    Serial.begin(115200);
    delay(400);
    /* 避免未使用的 STA 网卡与默认路由/DNS 与 SPI 网卡竞争 */
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);

    Serial.setDebugOutput(true);
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);

    ser("\r\n\r\n========== SPI host boot ==========");
    serf("reset: %s (%d)", reset_reason_str(esp_reset_reason()), (int)esp_reset_reason());
    serf("chip=%s cpu=%u MHz heap=%u", ESP.getChipModel(), (unsigned)ESP.getCpuFreqMHz(),
         (unsigned)esp_get_free_heap_size());

    ESP_LOGI(TAG, "SPI host Arduino | hdr=%u buf=%u", (unsigned)sizeof(struct esp_payload_header),
             (unsigned)SPI_BUFFER_SIZE);

    ser("[setup] create semaphores ...");
    s_spi_sem = xSemaphoreCreateBinary();
    s_spi_mutex = xSemaphoreCreateMutex();
    s_buffer_pool_mutex = xSemaphoreCreateMutex();
    s_tx_flow_sem = xSemaphoreCreateBinary();
    if (!s_spi_sem || !s_spi_mutex || !s_buffer_pool_mutex || !s_tx_flow_sem) {
        ESP_LOGE(TAG, "sem create failed");
        ser("[setup] ERROR semaphore create failed");
        return;
    }
    ser("[setup] semaphores OK");

    ser("[setup] create TX queues ...");
    for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
        s_tx_queues[i] = xQueueCreate(SPI_QUEUE_SIZE, sizeof(spi_host_tx_msg_t));
        if (!s_tx_queues[i]) {
            ESP_LOGE(TAG, "tx queue %d failed", i);
            serf("[setup] ERROR tx queue %d", i);
            return;
        }
    }
    ser("[setup] TX queues OK");

    s_eth_rxq = xQueueCreate(24, sizeof(eth_rx_msg_t));
    if (!s_eth_rxq) {
        ESP_LOGE(TAG, "eth rx queue failed");
        ser("[setup] ERROR eth_rx queue");
        return;
    }
    ser("[setup] eth_rx queue OK");

    ser("[setup] esp_netif_init ...");
    esp_err_t e1 = esp_netif_init();
    if (e1 != ESP_OK && e1 != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(e1));
        serf("[setup] ERROR esp_netif_init %s", esp_err_to_name(e1));
        return;
    }
    serf("[setup] esp_netif_init OK (%s)", esp_err_to_name(e1));

    ser("[setup] esp_event_loop_create_default ...");
    esp_err_t e2 = esp_event_loop_create_default();
    if (e2 != ESP_OK && e2 != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(e2));
        serf("[setup] ERROR esp_event %s", esp_err_to_name(e2));
        return;
    }
    serf("[setup] event loop OK (%s)", esp_err_to_name(e2));

    ser("[setup] init_network_interface ...");
    if (init_network_interface() != ESP_OK) {
        ser("[setup] ERROR init_network_interface failed");
        return;
    }

    ser("[setup] start eth_rx_dispatch on core 0 ...");
    xTaskCreatePinnedToCore(eth_rx_dispatch_task, "eth_rx", 4096, NULL, 6, NULL, 0);

    ser("[setup] spi_master_init_local ...");
    if (spi_master_init_local() != ESP_OK) {
        ser("[setup] ERROR spi_master_init_local failed");
        return;
    }

    ser("[setup] gpio_init_interrupt_driven ...");
    esp_err_t ge = gpio_init_interrupt_driven();
    if (ge != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR init failed: %s", esp_err_to_name(ge));
        serf("[setup] ERROR gpio %s", esp_err_to_name(ge));
        return;
    }

    ser("[setup] start spi_processing_task on core 1 ...");
    xTaskCreatePinnedToCore(spi_processing_task, "spi_proc", 4096, NULL, 5, NULL, 1);

    ser("[setup] data_path_control(true) ...");
    data_path_control(true);

    ser("[setup] start net_connectivity_test_task ...");
    xTaskCreatePinnedToCore(net_connectivity_test_task, "net_test", 8192, NULL, 4, NULL, 0);

    serf("[setup] DONE heap=%u | ~12s: ping 8.8.8.8 then HTTP baidu", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Ready. ~12s later: ping 8.8.8.8 then HTTP www.baidu.com (see serial log).");
}

void loop()
{
    static uint32_t t0;
    if (t0 == 0) {
        t0 = millis();
    }
    const uint32_t now = millis();
    if (now - t0 >= 5000) {
        t0 = now;
        serf("[loop] +%lu s heap=%u | TXpkt=%lu RXpkt=%lu skipHS=%lu dummyRX=%lu inv=%lu",
             (unsigned long)(now / 1000), (unsigned)esp_get_free_heap_size(),
             (unsigned long)s_stats.tx_packets, (unsigned long)s_stats.rx_packets,
             (unsigned long)s_stats.skip_hs_low, (unsigned long)s_stats.rx_dummy,
             (unsigned long)s_stats.invalid_packets);
        ESP_LOGI(TAG, "stats TX=%lu RX=%lu bytes tx=%lu rx=%lu skipHS=%lu dummy=%lu inv=%lu qfull=%lu",
                 (unsigned long)s_stats.tx_packets, (unsigned long)s_stats.rx_packets,
                 (unsigned long)s_stats.tx_bytes, (unsigned long)s_stats.rx_bytes,
                 (unsigned long)s_stats.skip_hs_low, (unsigned long)s_stats.rx_dummy,
                 (unsigned long)s_stats.invalid_packets, (unsigned long)s_stats.queue_full_count);
    }
    delay(50);
}
