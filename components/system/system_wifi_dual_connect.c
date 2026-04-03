/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "dhcpserver/dhcpserver.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/lwip_napt.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"

#include "system_wifi_dual_connect.h"

static const char *TAG = "wifi_dual";

typedef enum {
    UPLINK_NONE = 0,
    UPLINK_WIFI_STA,
    UPLINK_ETH_WAN,
} uplink_t;

static bool s_wifi_up = false;
static bool s_eth_up = false;
static uplink_t s_default = UPLINK_NONE;

static void softap_napt_refresh(void)
{
#if CONFIG_LWIP_IPV4_NAPT
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap) {
        return;
    }

    esp_netif_ip_info_t ipi;
    if (esp_netif_get_ip_info(ap, &ipi) != ESP_OK) {
        return;
    }

    /* ip_napt_enable(addr) matches netif by address; endian/timing can miss and then lwIP returns
     * without enabling NAPT. Bind directly to the SoftAP lwIP netif (IDF lwIP API). */
    struct netif *lw = (struct netif *)esp_netif_get_netif_impl(ap);
    if (!lw || !netif_is_up(lw)) {
        ESP_LOGW(TAG, "SoftAP lwIP netif missing or down; NAPT skipped");
        return;
    }
    if (!ip_napt_enable_netif(lw, 1)) {
        ESP_LOGE(TAG, "ip_napt_enable_netif(SoftAP) failed");
        /* last resort */
        ip_napt_enable(ipi.ip.addr, 1);
    }
    ESP_LOGI(TAG, "NAPT enabled on SoftAP " IPSTR " (netif %c%c%d)", IP2STR(&ipi.ip), lw->name[0],
             lw->name[1], (int)lw->num);
#endif
}

/**
 * Mirrors esp_bridge_create_netif() DHCP server options for SoftAP (see bridge_common.c).
 * The Wi-Fi SoftAP path only enables OFFER_DNS; without router + lease pool many phones
 * will not get a usable default gateway after bridge updates DNS (which also deauths clients).
 *
 * Must be called after `esp_bridge_create_all_netif()` because it depends on WIFI_AP_DEF.
 */
static void softap_dhcps_full_config(void)
{
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap) {
        ESP_LOGW(TAG, "WIFI_AP_DEF missing, skip SoftAP DHCPS");
        return;
    }

    esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;
    ESP_ERROR_CHECK(esp_netif_dhcps_get_status(ap, &st));
    if (st != ESP_NETIF_DHCP_STOPPED) {
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap));
    }

    dhcps_offer_t dhcps_dns_value = OFFER_DNS;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                          &dhcps_dns_value, sizeof(dhcps_dns_value)));

    esp_netif_ip_info_t ap_ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap, &ap_ip));
    uint32_t net_base = ap_ip.ip.addr & PP_HTONL(0xffffff00);
    ip4_addr_t pool_start;
    ip4_addr_t pool_end;
    ip4_addr_set_u32(&pool_start, net_base | PP_HTONL(0x02));
    ip4_addr_set_u32(&pool_end, net_base | PP_HTONL(0x09));

    dhcps_lease_t lease = {
        .enable = true,
        .start_ip = pool_start,
        .end_ip = pool_end,
    };
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS,
                                          &lease, sizeof(lease)));

    uint8_t offer_router = 1;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,
                                          &offer_router, sizeof(offer_router)));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
    ESP_LOGI(TAG, "SoftAP DHCPS: DNS offer + lease pool + router (gw) reapplied");
    softap_napt_refresh();
}

static esp_netif_t *get_uplink_netif(uplink_t u)
{
    if (u == UPLINK_WIFI_STA) return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (u == UPLINK_ETH_WAN) return esp_netif_get_handle_from_ifkey("ETH_WAN");
    return NULL;
}

static uplink_t choose_default_uplink(void)
{
    // Step (1)/(2): if only one uplink is up, choose it.
    // If both are up (step (3) later), prefer ETH_WAN by default.
    if (s_eth_up) return UPLINK_ETH_WAN;
    if (s_wifi_up) return UPLINK_WIFI_STA;
    return UPLINK_NONE;
}

static void apply_default_route(uplink_t u)
{
    if (u == s_default) return;

    esp_netif_t *netif = get_uplink_netif(u);
    if (!netif) {
        ESP_LOGW(TAG, "default uplink netif missing (%d)", (int)u);
        return;
    }

    esp_netif_set_default_netif(netif);
    s_default = u;
    ESP_LOGI(TAG, "Default route: uplink %s", esp_netif_get_ifkey(netif));
}

static void on_uplink_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    if (id == IP_EVENT_STA_GOT_IP) s_wifi_up = true;
    if (id == IP_EVENT_ETH_GOT_IP) s_eth_up = true;

    // Update default route according to current policy.
    apply_default_route(choose_default_uplink());

    // After iot-bridge handler (DNS update / possible deauth): reapply SoftAP DHCP/NAPT.
    softap_dhcps_full_config();

    /* APSTA + Ethernet/NAPT: keep Wi-Fi powersave off so SoftAP ↔ coexisting STA path does not
     * stall forwarded traffic when STA is idle (Ethernet is primary uplink here). */
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(NONE): %s", esp_err_to_name(err));
    }
}

static void on_uplink_lost_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == IP_EVENT_STA_LOST_IP) s_wifi_up = false;
    if (id == IP_EVENT_ETH_LOST_IP) s_eth_up = false;

    apply_default_route(choose_default_uplink());
    softap_dhcps_full_config();
}

void system_wifi_dual_connect_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_uplink_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_uplink_got_ip, NULL));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_uplink_lost_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &on_uplink_lost_ip, NULL));
#endif

    // Initial SoftAP DHCP/NAPT setup.
    softap_dhcps_full_config();
}

