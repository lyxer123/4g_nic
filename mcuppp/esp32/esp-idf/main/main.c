#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "ppp_client";

#define PPP_UART_NUM UART_NUM_2
#define PPP_UART_RX_PIN 16
#define PPP_UART_TX_PIN 17
#define PPP_UART_BAUD 115200

static esp_netif_t *s_ppp_netif = NULL;
static TaskHandle_t s_uart_task = NULL;
static bool http_test_done = false;

static esp_err_t uart_transmit(void *h, void *buffer, size_t len)
{
    uart_port_t port = (uart_port_t)(uintptr_t)h;
    const uint8_t *data = (const uint8_t *)buffer;
    size_t remaining = len;

    while (remaining > 0) {
        int written = uart_write_bytes(port, (const char *)data, remaining);
        if (written < 0) {
            return ESP_FAIL;
        }
        remaining -= (size_t)written;
        data += written;
    }
    return ESP_OK;
}

static void uart_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[1024];

    ESP_LOGI(TAG, "PPP UART reader task started on UART%u", PPP_UART_NUM);

    while (true) {
        int len = uart_read_bytes(PPP_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len > 0 && s_ppp_netif != NULL) {
            esp_netif_receive(s_ppp_netif, buf, len, NULL);
        }
    }
}

static void http_get_test(void)
{
    esp_http_client_config_t config = {
        .url = "http://www.baidu.com",
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP GET status=%d, content_length=%d", status, content_len);

        char buffer[257] = {0};
        int read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
        if (read_len > 0) {
            buffer[read_len] = '\0';
            ESP_LOGI(TAG, "HTTP response preview:\n%s", buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void ppp_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    esp_netif_t *netif = (esp_netif_t *)arg;
    if (netif == NULL) {
        return;
    }

    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "PPP got IP " IPSTR, IP2STR(&ip_info.ip));
            if (!http_test_done) {
                http_get_test();
                http_test_done = true;
            }
        }
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGW(TAG, "PPP lost IP");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    uart_config_t uart_cfg = {
        .baud_rate = PPP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(PPP_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(PPP_UART_NUM, PPP_UART_TX_PIN, PPP_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PPP_UART_NUM, 4096, 4096, 0, NULL, 0));

    esp_netif_config_t ppp_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&ppp_cfg);
    if (s_ppp_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create PPP netif");
        return;
    }

    esp_netif_set_default_netif(s_ppp_netif);

    esp_netif_driver_ifconfig_t driver_ifconfig = {
        .transmit = uart_transmit,
        .handle = (void *)(uintptr_t)PPP_UART_NUM,
    };
    ESP_ERROR_CHECK(esp_netif_set_driver_config(s_ppp_netif, &driver_ifconfig));

    esp_netif_ppp_config_t ppp_params = {
        .ppp_phase_event_enabled = false,
        .ppp_error_event_enabled = true,
    };
    ESP_ERROR_CHECK(esp_netif_ppp_set_params(s_ppp_netif, &ppp_params));
    ESP_ERROR_CHECK(esp_netif_ppp_set_auth(s_ppp_netif, NETIF_PPP_AUTHTYPE_NONE, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_GOT_IP,
                                                       &ppp_event_handler,
                                                       s_ppp_netif,
                                                       NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_LOST_IP,
                                                       &ppp_event_handler,
                                                       s_ppp_netif,
                                                       NULL));

    esp_netif_action_start(s_ppp_netif, NULL, 0, NULL);

    xTaskCreate(uart_rx_task, "ppp_uart_rx", 4096, NULL, 5, &s_uart_task);

    ESP_LOGI(TAG, "PPP client started on UART%u", PPP_UART_NUM);
}
