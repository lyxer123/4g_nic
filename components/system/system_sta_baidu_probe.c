/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "system_sta_baidu_probe.h"

static const char *TAG = "sta_probe";

#if CONFIG_APP_STA_BAIDU_HTTP_PROBE

static void baidu_probe_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(300));

    esp_http_client_config_t cfg = {
        .url = "http://www.baidu.com/",
        .timeout_ms = 15000,
        .addr_type = HTTP_ADDR_TYPE_INET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP init failed");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);

    if (err == ESP_OK && status > 0) {
        ESP_LOGI(TAG, "www.baidu.com OK: HTTP status=%d content_length=%d", status, content_len);
    } else {
        ESP_LOGW(TAG, "www.baidu.com FAIL: err=%s status=%d", esp_err_to_name(err), status);
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

static void on_sta_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    const uint32_t stack = 6144;
    if (xTaskCreate(baidu_probe_task, "baidu_probe", stack, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(baidu_probe) failed");
    }
}

#endif /* CONFIG_APP_STA_BAIDU_HTTP_PROBE */

void system_sta_baidu_probe_init(void)
{
#if CONFIG_APP_STA_BAIDU_HTTP_PROBE
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_sta_got_ip, NULL));
#endif
}
