/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "sdkconfig.h"

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "esp_bridge.h"

#include "nvs.h"

#include "system_bridge_runtime.h"
#include "system_usb_cat1_detect.h"
#include "system_w5500_detect.h"
#include "system_mode_manager.h"

static const char *TAG = "bridge_rt";

static uint8_t peek_saved_work_mode_u8(void)
{
    nvs_handle_t h = 0;
    uint8_t m = 0;
    if (nvs_open("bridge_ui", NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    if (nvs_get_u8(h, "work_mode", &m) != ESP_OK) {
        m = 0;
    }
    nvs_close(h);
    return m;
}

void system_bridge_init_netifs_from_hw(void)
{
    ESP_LOGI(TAG, "netifs by HW: w5500=%d usb_cat1=%d", (int)system_w5500_detect_present(),
             (int)system_usb_cat1_detect_present());

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN) || \
    defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET)
    /* If NVS has a valid saved mode, prefer creating only the required Ethernet role(s) on boot.
     * This avoids bringing up ETH_LAN first when the device should immediately act as ETH_WAN (mode 7). */
    bool create_eth_lan = false;
    bool create_eth_wan = system_w5500_detect_present();
    const uint8_t saved = peek_saved_work_mode_u8();
    const system_mode_profile_t *sp = saved ? system_mode_manager_get_profile(saved) : NULL;
    if (saved && sp && system_mode_manager_mode_allowed(saved)) {
        if (sp->wan_type == SYSTEM_WAN_W5500) {
            create_eth_wan = true;
            create_eth_lan = false;
        } else if (sp->lan_eth) {
            create_eth_lan = true;
            create_eth_wan = false;
        } else {
            create_eth_lan = false;
            create_eth_wan = false;
        }
        ESP_LOGI(TAG, "saved work_mode=%u → create ETH_LAN=%d ETH_WAN=%d", (unsigned)saved, (int)create_eth_lan,
                 (int)create_eth_wan);
    } else if (saved) {
        ESP_LOGW(TAG, "saved work_mode=%u invalid/blocked → create ETH_WAN=%d ETH_LAN=%d for fallback",
                 (unsigned)saved, (int)create_eth_wan, (int)create_eth_lan);
    } else {
        ESP_LOGI(TAG, "no saved work_mode → create ETH_WAN=%d ETH_LAN=%d (SoftAP provisioning first)",
                 (int)create_eth_wan, (int)create_eth_lan);
    }
#else
    const bool create_eth_lan = true;
    const bool create_eth_wan = true;
#endif

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SOFTAP)
    esp_bridge_create_softap_netif(NULL, NULL, true, true);
#if defined(CONFIG_BRIDGE_WIFI_PMF_DISABLE)
    esp_wifi_disable_pmf_config(WIFI_IF_AP);
    ESP_LOGI(TAG, "DHCPS Restart, deauth all station");
    esp_wifi_deauth_sta(0);
#endif
#endif

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_USB)
    esp_bridge_create_usb_netif(NULL, NULL, true, true);
#endif

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SPI)
    esp_bridge_create_spi_netif(NULL, NULL, true, true);
#elif defined(CONFIG_BRIDGE_EXTERNAL_NETIF_SPI)
    uint8_t spi_mac[6] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6};
    esp_bridge_create_spi_netif(NULL, spi_mac, false, false);
#endif

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SDIO)
    esp_bridge_create_sdio_netif(NULL, NULL, true, true);
#elif defined(CONFIG_BRIDGE_EXTERNAL_NETIF_SDIO)
    uint8_t sdio_mac[6] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6};
    esp_bridge_create_sdio_netif(NULL, sdio_mac, false, false);
#endif

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (system_w5500_detect_present()) {
        if (create_eth_lan) {
            esp_bridge_create_eth_netif(NULL, NULL, true, true);
        } else {
            ESP_LOGI(TAG, "skip ETH (LAN role): not required by saved mode");
        }
    } else {
        ESP_LOGI(TAG, "skip ETH (LAN role): W5500 not detected");
    }
#endif

#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (system_w5500_detect_present()) {
        if (create_eth_wan) {
            esp_bridge_create_eth_netif(NULL, NULL, false, false);
        } else {
            ESP_LOGI(TAG, "skip ETH (WAN role): not required by saved mode");
        }
    } else {
        ESP_LOGI(TAG, "skip ETH (WAN role): W5500 not detected");
    }
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM)
#if CONFIG_SYSTEM_ALWAYS_CREATE_MODEM_NETIF
    esp_bridge_create_modem_netif(NULL, NULL, false, false);
#else
    if (system_usb_cat1_detect_present()) {
        esp_bridge_create_modem_netif(NULL, NULL, false, false);
    } else {
        ESP_LOGI(TAG, "skip modem netif: USB LTE not detected (CONFIG_SYSTEM_ALWAYS_CREATE_MODEM_NETIF=n)");
    }
#endif
#endif
#endif

#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_STATION)
    esp_bridge_create_station_netif(NULL, NULL, false, false);
#if defined(CONFIG_BRIDGE_WIFI_PMF_DISABLE)
    esp_wifi_disable_pmf_config(WIFI_IF_STA);
#endif
#endif
}
