#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// SPI Configuration
#define SPI_MOSI_PIN    23
#define SPI_MISO_PIN    19
#define SPI_CLK_PIN     18
#define SPI_CS_PIN      5
#define SPI_HANDSHAKE_PIN 4
#define DATA_READY_PIN  2

#define SPI_TRANSACTION_LEN 256

static const char *TAG = "spi_ppp_bridge";

static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ppp_netif = NULL;
static bool wifi_connected = false;
static bool ppp_connected = false;

// SPI slave variables
static spi_slave_transaction_t trans;
static uint8_t spi_tx_buf[SPI_TRANSACTION_LEN];
static uint8_t spi_rx_buf[SPI_TRANSACTION_LEN];

// PPP over SPI transmit function
static esp_err_t spi_ppp_transmit(void *h, void *buffer, size_t len)
{
    if (!buffer || len == 0 || len > SPI_TRANSACTION_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clear buffers
    memset(spi_tx_buf, 0, SPI_TRANSACTION_LEN);
    memset(spi_rx_buf, 0, SPI_TRANSACTION_LEN);
    
    // Copy data to transmit buffer
    memcpy(spi_tx_buf, buffer, len);
    
    // Set transaction length
    trans.length = len * 8; // length in bits
    trans.tx_buffer = spi_tx_buf;
    trans.rx_buffer = spi_rx_buf;
    
    // Wait for handshake from host
    if (gpio_get_level(SPI_HANDSHAKE_PIN) == 0) {
        // Signal data ready
        gpio_set_level(DATA_READY_PIN, 1);
        
        // Wait for CS to go low (transaction start)
        int timeout = 1000;
        while (gpio_get_level(SPI_CS_PIN) == 1 && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        if (timeout > 0) {
            // Perform SPI transaction
            esp_err_t ret = spi_slave_transmit(VSPI_HOST, &trans, portMAX_DELAY);
            
            // Clear data ready signal
            gpio_set_level(DATA_READY_PIN, 0);
            
            return ret;
        } else {
            gpio_set_level(DATA_READY_PIN, 0);
            return ESP_ERR_TIMEOUT;
        }
    }
    
    return ESP_ERR_INVALID_STATE;
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected, trying to reconnect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected, got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// PPP event handler
static void ppp_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ppp_netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "PPP got IP: " IPSTR, IP2STR(&ip_info.ip));
            ppp_connected = true;
        }
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "PPP lost IP");
        ppp_connected = false;
    }
}

// Initialize WiFi station
void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi station initialization finished");
}

// Initialize SPI slave interface
void spi_slave_init(void)
{
    // GPIO configuration
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << DATA_READY_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << SPI_HANDSHAKE_PIN) | (1ULL << SPI_CS_PIN);
    gpio_config(&io_conf);
    
    // Set initial states
    gpio_set_level(DATA_READY_PIN, 0);
    
    // SPI slave configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    
    spi_slave_interface_config_t slv_cfg = {
        .mode = 0,
        .spics_io_num = SPI_CS_PIN,
        .queue_size = 3,
        .flags = 0,
    };
    
    ESP_ERROR_CHECK(spi_slave_initialize(VSPI_HOST, &bus_cfg, &slv_cfg, SPI_DMA_CH_AUTO));
    
    // Initialize transaction structure
    memset(&trans, 0, sizeof(trans));
    
    ESP_LOGI(TAG, "SPI slave interface initialized");
}

// Initialize PPP over SPI
void ppp_spi_init(void)
{
    // Create PPP network interface
    esp_netif_config_t ppp_cfg = ESP_NETIF_DEFAULT_PPP();
    ppp_netif = esp_netif_new(&ppp_cfg);
    
    if (ppp_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create PPP netif");
        return;
    }
    
    // Configure SPI as PPP transport
    esp_netif_driver_ifconfig_t driver_ifconfig = {
        .handle = (void*)VSPI_HOST,
        .transmit = spi_ppp_transmit,
    };
    
    ESP_ERROR_CHECK(esp_netif_set_driver_config(ppp_netif, &driver_ifconfig));
    ESP_ERROR_CHECK(esp_netif_attach(ppp_netif, (esp_netif_iodriver_handle)VSPI_HOST));
    
    // Configure PPP parameters
    esp_netif_ppp_config_t ppp_params = {
        .ppp_phase_event_enabled = false,
        .ppp_error_event_enabled = true,
    };
    
    ESP_ERROR_CHECK(esp_netif_ppp_set_params(ppp_netif, &ppp_params));
    ESP_ERROR_CHECK(esp_netif_ppp_set_auth(ppp_netif, NETIF_PPP_AUTHTYPE_NONE, NULL, NULL));
    
    // Register PPP event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_GOT_IP,
                                                       &ppp_event_handler,
                                                       NULL,
                                                       NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_LOST_IP,
                                                       &ppp_event_handler,
                                                       NULL,
                                                       NULL));
    
    // Start PPP
    ESP_ERROR_CHECK(esp_netif_action_start(ppp_netif, NULL, 0, NULL));
    
    ESP_LOGI(TAG, "PPP over SPI initialized");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 SPI PPP Bridge");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();
    
    // Initialize SPI slave interface
    spi_slave_init();
    
    // Initialize PPP over SPI
    ppp_spi_init();
    
    ESP_LOGI(TAG, "SPI PPP Bridge initialization complete");
    ESP_LOGI(TAG, "Waiting for SPI host connection...");
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Log status periodically
        ESP_LOGI(TAG, "Status - WiFi: %s, PPP: %s", 
                 wifi_connected ? "Connected" : "Disconnected",
                 ppp_connected ? "Connected" : "Disconnected");
    }
}
