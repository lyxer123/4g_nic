/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"

#include "system_hw_presence.h"
#include "system_w5500_detect.h"
#include "system_usb_cat1_detect.h"

static const char *TAG = "hw_pres";

void system_hw_presence_probe_before_bridge(void)
{
    ESP_LOGI(TAG, "probe_before_bridge: start (W5500 detect then USB Cat1 host probe)");
#if CONFIG_SYSTEM_HW_DETECT_W5500
    (void)system_w5500_detect_run();
    ESP_LOGI(TAG, "probe_before_bridge: W5500 detect returned");
#endif
    /* Always invoke: implementation is a no-op/stub when SYSTEM_USB_CAT1_DETECT is off.
     * When on, runs regardless of CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM (for web UI / logs). */
    (void)system_usb_cat1_detect_run();
    ESP_LOGI(TAG, "probe_before_bridge: done");
}
