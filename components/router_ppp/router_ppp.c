#include "router_ppp.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

#if CONFIG_LWIP_IPV4_NAPT
#include "lwip/lwip_napt.h"
void ip_napt_table_clear(void);
#endif

static const char *TAG = "router_ppp";

#if defined(CONFIG_ROUTER_PPP_ENABLE) && CONFIG_ROUTER_PPP_ENABLE
static esp_netif_t *s_ppp_netif = NULL;
static uart_port_t s_ppp_uart_port =
#ifdef CONFIG_ROUTER_PPP_UART_NUM
    (uart_port_t)CONFIG_ROUTER_PPP_UART_NUM;
#else
    UART_NUM_2;
#endif
static TaskHandle_t s_ppp_uart_task = NULL;
static esp_netif_driver_base_t s_ppp_netif_driver = {0};

static esp_err_t router_ppp_uart_transmit(void *h, void *buffer, size_t len)
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

static void router_ppp_uart_task(void *arg)
{
    (void)arg;
    uint8_t buf[2048];

    ESP_LOGI(TAG, "PPP UART task started on UART%u", (unsigned)s_ppp_uart_port);

    while (true) {
        int len = uart_read_bytes(s_ppp_uart_port, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len > 0) {
            if (s_ppp_netif) {
                esp_netif_receive(s_ppp_netif, buf, len, NULL);
            }
        }
    }
}

static void router_ppp_ip_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    esp_netif_t *netif = (esp_netif_t *)arg;
    if (!netif) {
        return;
    }

    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_ip_info_t netif_ip;
        if (esp_netif_get_ip_info(netif, &netif_ip) == ESP_OK) {
            ESP_LOGI(TAG, "PPPoS server got local IP " IPSTR,
                     IP2STR(&netif_ip.ip));
#if CONFIG_LWIP_IPV4_NAPT
            ip_napt_enable(netif_ip.ip.addr, 1);
            ESP_LOGI(TAG, "PPPoS NAT enabled for " IPSTR, IP2STR(&netif_ip.ip));
#endif
        }
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "PPPoS server lost IP");
#if CONFIG_LWIP_IPV4_NAPT
        ip_napt_table_clear();
#endif
    }
}
#endif

void router_ppp_start(void)
{
#if CONFIG_ROUTER_PPP_ENABLE
    if (s_ppp_netif != NULL) {
        ESP_LOGW(TAG, "PPPoS server already started");
        return;
    }

    if (s_ppp_uart_port == (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM) {
        ESP_LOGE(TAG, "PPPoS UART must not share the console UART");
        return;
    }
#if defined(CONFIG_ROUTER_AT_ENABLE) && CONFIG_ROUTER_AT_ENABLE
    if (s_ppp_uart_port == (uart_port_t)CONFIG_ROUTER_AT_UART_NUM) {
        ESP_LOGE(TAG, "PPPoS UART must not share the router AT UART");
        return;
    }
#endif

    if (uart_is_driver_installed(s_ppp_uart_port)) {
        ESP_LOGE(TAG, "UART%u driver already installed; cannot start PPPoS server", (unsigned)s_ppp_uart_port);
        return;
    }

    uart_config_t uart_cfg = {
        .baud_rate = CONFIG_ROUTER_PPP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(s_ppp_uart_port, &uart_cfg));
    if (CONFIG_ROUTER_PPP_UART_TX_GPIO >= 0 && CONFIG_ROUTER_PPP_UART_RX_GPIO >= 0) {
        ESP_ERROR_CHECK(uart_set_pin(s_ppp_uart_port, CONFIG_ROUTER_PPP_UART_TX_GPIO,
                                     CONFIG_ROUTER_PPP_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE));
    }

    ESP_ERROR_CHECK(uart_driver_install(s_ppp_uart_port, 4096, 2048, 0, NULL, 0));

    esp_netif_config_t ppp_config = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&ppp_config);
    if (s_ppp_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create PPP netif");
        uart_driver_delete(s_ppp_uart_port);
        return;
    }

    esp_netif_driver_ifconfig_t driver_ifconfig = {
        .transmit = router_ppp_uart_transmit,
        .handle = (void *)(uintptr_t)s_ppp_uart_port,
    };

    ESP_ERROR_CHECK(esp_netif_set_driver_config(s_ppp_netif, &driver_ifconfig));

    s_ppp_netif_driver.transmit = router_ppp_uart_transmit;
    s_ppp_netif_driver.handle = (void *)(uintptr_t)s_ppp_uart_port;
    ESP_ERROR_CHECK(esp_netif_attach(s_ppp_netif, &s_ppp_netif_driver));

    esp_netif_ppp_config_t ppp_params = {
        .ppp_phase_event_enabled = false,
        .ppp_error_event_enabled = true,
    };
#if defined(CONFIG_LWIP_PPP_SERVER_SUPPORT)
    IP4_ADDR(&ppp_params.ppp_our_ip4_addr, 192, 168, 100, 1);
    IP4_ADDR(&ppp_params.ppp_their_ip4_addr, 192, 168, 100, 2);
#endif
    ESP_ERROR_CHECK(esp_netif_ppp_set_params(s_ppp_netif, &ppp_params));
    ESP_ERROR_CHECK(esp_netif_ppp_set_auth(s_ppp_netif, NETIF_PPP_AUTHTYPE_NONE, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_GOT_IP,
                                                       &router_ppp_ip_event_handler,
                                                       s_ppp_netif,
                                                       NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_LOST_IP,
                                                       &router_ppp_ip_event_handler,
                                                       s_ppp_netif,
                                                       NULL));

    esp_err_t start_err = esp_netif_action_start(s_ppp_netif, NULL, 0, NULL);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PPPoS server: %s", esp_err_to_name(start_err));
        esp_netif_destroy(s_ppp_netif);
        s_ppp_netif = NULL;
        uart_driver_delete(s_ppp_uart_port);
        return;
    }

    if (xTaskCreate(router_ppp_uart_task, "router_ppp_uart", 4096, NULL, 4, &s_ppp_uart_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PPPoS UART task");
        esp_netif_destroy(s_ppp_netif);
        s_ppp_netif = NULL;
        uart_driver_delete(s_ppp_uart_port);
        return;
    }

    ESP_LOGI(TAG, "PPPoS server started on UART%u", (unsigned)s_ppp_uart_port);
#else
    (void)TAG;
#endif
}

bool router_ppp_is_running(void)
{
#if CONFIG_ROUTER_PPP_ENABLE
    return s_ppp_netif != NULL;
#else
    return false;
#endif
}

esp_netif_t *router_ppp_get_netif(void)
{
#if CONFIG_ROUTER_PPP_ENABLE
    return s_ppp_netif;
#else
    return NULL;
#endif
}
