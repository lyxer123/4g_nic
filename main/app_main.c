/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "web_service.h"
#include "serial_cli.h"
#include "router_at.h"
#include "router_ppp.h"
#include "system_hw_presence.h"
#include "system_mode_manager.h"
#include "system_bridge_runtime.h"
#include "system_wifi_dual_connect.h"
#include "system_sta_baidu_probe.h"
#include "system_eth_uplink_debug.h"
#include "system_stability.h"

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

void app_main(void)
{
    esp_storage_init();

    /* Init and register system/core components */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    system_hw_presence_probe_before_bridge();

    /* Read/log work_mode before netifs; apply runs after bridge creates SoftAP / STA / Ethernet. */
    system_mode_manager_log_startup_plan();

    system_bridge_init_netifs_from_hw();
    (void)system_mode_manager_apply_saved_or_hw_default();
    system_wifi_dual_connect_init();
    web_softap_restore_from_nvs();
    system_sta_baidu_probe_init();
    system_eth_uplink_debug_init();
    (void)system_stability_init();
    ESP_ERROR_CHECK(web_service_start());
    serial_cli_start();
    router_at_start();
    router_ppp_start();
}
