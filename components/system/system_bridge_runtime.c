/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "esp_bridge.h"

#include "system_bridge_runtime.h"
#include "system_usb_cat1_detect.h"
#include "system_w5500_detect.h"

static const char *TAG = "bridge_rt";

void system_bridge_init_netifs_from_hw(void)
{
    ESP_LOGI(TAG, "netifs by HW: w5500=%d usb_cat1=%d", (int)system_w5500_detect_present(),
             (int)system_usb_cat1_detect_present());

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
        esp_bridge_create_eth_netif(NULL, NULL, true, true);
    } else {
        ESP_LOGI(TAG, "skip ETH (LAN role): W5500 not detected");
    }
#endif

#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (system_w5500_detect_present()) {
        esp_bridge_create_eth_netif(NULL, NULL, false, false);
    } else {
        ESP_LOGI(TAG, "skip ETH (WAN role): W5500 not detected");
    }
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM)
    /* Modem PPP netif is independent of boot USB Cat.1 enumeration; esp_modem attaches when device appears. */
    esp_bridge_create_modem_netif(NULL, NULL, false, false);
#endif
#endif

#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_STATION)
    esp_bridge_create_station_netif(NULL, NULL, false, false);
#if defined(CONFIG_BRIDGE_WIFI_PMF_DISABLE)
    esp_wifi_disable_pmf_config(WIFI_IF_STA);
#endif
#endif
}
