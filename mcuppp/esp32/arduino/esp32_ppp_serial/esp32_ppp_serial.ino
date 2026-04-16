#include <HardwareSerial.h>
#include <HTTPClient.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "driver/uart.h"
#include "esp_log.h"

HardwareSerial pppSerial(2);

const int PPP_RX_PIN = 16;
const int PPP_TX_PIN = 17;
const int PPP_BAUD = 115200;

static esp_netif_t *s_ppp_netif = NULL;
static bool ppp_ready = false;

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

static void ppp_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_ppp_netif, &ip_info) == ESP_OK) {
            Serial.printf("PPP got IP " IPSTR "\n", IP2STR(&ip_info.ip));
            ppp_ready = true;
        }
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        Serial.println("PPP lost IP");
        ppp_ready = false;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }

    Serial.println("ESP32 Arduino PPP client example");
    pppSerial.begin(PPP_BAUD, SERIAL_8N1, PPP_RX_PIN, PPP_TX_PIN);
    Serial.printf("Serial2 started: RX=%d TX=%d @ %d\n", PPP_RX_PIN, PPP_TX_PIN, PPP_BAUD);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t ppp_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&ppp_cfg);
    if (s_ppp_netif == NULL) {
        Serial.println("Failed to create PPP netif");
        return;
    }

    esp_netif_driver_ifconfig_t driver_ifconfig = {
        .handle = (void *)(uintptr_t)UART_NUM_2,
        .transmit = uart_transmit,
    };
    ESP_ERROR_CHECK(esp_netif_set_driver_config(s_ppp_netif, &driver_ifconfig));

    ESP_ERROR_CHECK(esp_netif_attach(s_ppp_netif, (esp_netif_iodriver_handle)(uintptr_t)UART_NUM_2));

    esp_netif_ppp_config_t ppp_params = {
        .ppp_phase_event_enabled = false,
        .ppp_error_event_enabled = true,
    };
    ESP_ERROR_CHECK(esp_netif_ppp_set_params(s_ppp_netif, &ppp_params));
    ESP_ERROR_CHECK(esp_netif_ppp_set_auth(s_ppp_netif, NETIF_PPP_AUTHTYPE_NONE, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_GOT_IP,
                                                       &ppp_event_handler,
                                                       NULL,
                                                       NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_PPP_LOST_IP,
                                                       &ppp_event_handler,
                                                       NULL,
                                                       NULL));

    esp_netif_action_start(s_ppp_netif, NULL, 0, NULL);
}

void loop() {
    static bool did_fetch = false;

    if (ppp_ready && !did_fetch) {
        Serial.println("PPP connected, starting HTTP GET test...");

        HTTPClient http;
        http.begin("http://www.baidu.com");
        int httpCode = http.GET();
        Serial.printf("HTTP GET code: %d\n", httpCode);
        if (httpCode > 0) {
            String payload = http.getString();
            Serial.println("Response payload preview:");
            Serial.println(payload.substring(0, min((int)payload.length(), 256)));
        } else {
            Serial.printf("HTTP request failed: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();

        did_fetch = true;
    }

    while (pppSerial.available()) {
        int c = pppSerial.read();
        Serial.write((uint8_t)c);
    }

    while (Serial.available()) {
        int c = Serial.read();
        pppSerial.write((uint8_t)c);
    }

    delay(100);
}
