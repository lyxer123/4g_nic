/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#if CONFIG_BRIDGE_EXTERNAL_NETIF_STATION
#include "esp_wifi.h"
#endif

#include "esp_bridge.h"
#include "web_service.h"
#include "system_wifi_dual_connect.h"
#include "system_sta_baidu_probe.h"
#if CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET
#include "system_eth_uplink_debug.h"
#endif

static esp_err_t esp_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

#if CONFIG_BRIDGE_EXTERNAL_NETIF_STATION
static const char *TAG_APP = "app";

static void app_apply_default_wifi_sta(void)
{
    wifi_config_t w = {0};
    strlcpy((char *)w.sta.ssid, CONFIG_APP_WIFI_STA_SSID, sizeof(w.sta.ssid));
    strlcpy((char *)w.sta.password, CONFIG_APP_WIFI_STA_PASSWORD, sizeof(w.sta.password));
    w.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &w);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_APP, "esp_wifi_set_config(STA): %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_connect();
    ESP_LOGI(TAG_APP, "Wi-Fi STA SSID=%s connect: %s", CONFIG_APP_WIFI_STA_SSID, esp_err_to_name(err));
}
#endif

void app_main(void)
{
    esp_storage_init();

    /* Init and register system/core components */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_bridge_create_all_netif();
#if CONFIG_BRIDGE_EXTERNAL_NETIF_STATION
    app_apply_default_wifi_sta();
#endif
    system_wifi_dual_connect_init();
#if CONFIG_BRIDGE_EXTERNAL_NETIF_STATION
    system_sta_baidu_probe_init();
#endif
#if CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET
    system_eth_uplink_debug_init();
#endif
    ESP_ERROR_CHECK(web_service_start());
}
