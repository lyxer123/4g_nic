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
#include "lwip/lwip_napt.h"

#include "esp_event.h"
#include "esp_netif.h"

#include "system_wifi_dual_connect.h"

static const char *TAG = "wifi_dual";

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

    ip_napt_enable(ipi.ip.addr, 1);
    ESP_LOGI(TAG, "NAPT enabled on SoftAP " IPSTR, IP2STR(&ipi.ip));
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

static void on_uplink_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    if (ev && ev->esp_netif) {
        esp_netif_set_default_netif(ev->esp_netif);
        ESP_LOGI(TAG, "Default route: uplink %s", esp_netif_get_ifkey(ev->esp_netif));
    }

    // After iot-bridge handler (DNS update / possible deauth): reapply SoftAP DHCP/NAPT.
    softap_dhcps_full_config();

    // Better NAT/throughput.
    if (id == IP_EVENT_STA_GOT_IP) {
        esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_set_ps(NONE): %s", esp_err_to_name(err));
        }
    }
}

void system_wifi_dual_connect_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_uplink_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_uplink_got_ip, NULL));

    // Initial SoftAP DHCP/NAPT setup.
    softap_dhcps_full_config();
}

