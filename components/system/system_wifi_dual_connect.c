/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "dhcpserver/dhcpserver.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/lwip_napt.h"

#include "esp_event.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#if CONFIG_LWIP_PPP_SUPPORT
#include "esp_netif_ppp.h"
#endif

#include "system_wifi_dual_connect.h"

static const char *TAG = "wifi_dual";

typedef enum {
    UPLINK_NONE = 0,
    UPLINK_WIFI_STA,
    UPLINK_ETH_WAN,
    UPLINK_USB_MODEM,
} uplink_t;

static bool s_wifi_up = false;
static bool s_eth_up = false;
static bool s_ppp_up = false;
static uplink_t s_default = UPLINK_NONE;

/** ETH_LAN 有线口 Up 后等待链路稳定再全量 DHCPS，避免 PC 网卡频繁启停导致连续 stop/start SoftAP DHCP。 */
#define ETH_LAN_LINK_DEBOUNCE_US (400 * 1000)

static bool s_eth_lan_link_up = false;
static esp_timer_handle_t s_eth_lan_debounce_timer;

static const char *uplink_t_name(uplink_t u)
{
    switch (u) {
    case UPLINK_WIFI_STA: return "WIFI_STA";
    case UPLINK_ETH_WAN: return "ETH_WAN";
    case UPLINK_USB_MODEM: return "PPP";
    default: return "none";
    }
}

static const char *ip_got_event_label(int32_t id, const ip_event_got_ip_t *ev)
{
    static char buf[40];
    const char *ifk = (ev && ev->esp_netif) ? esp_netif_get_ifkey(ev->esp_netif) : "";
    switch (id) {
    case IP_EVENT_STA_GOT_IP:
        return "STA_GOT_IP";
    case IP_EVENT_ETH_GOT_IP:
        snprintf(buf, sizeof(buf), "ETH_GOT_IP %s", ifk[0] ? ifk : "?");
        return buf;
#if CONFIG_LWIP_PPP_SUPPORT
    case IP_EVENT_PPP_GOT_IP:
        return "PPP_GOT_IP";
#endif
    default:
        return "?";
    }
}

static const char *ip_lost_event_label(int32_t id, const ip_event_got_ip_t *ev)
{
    static char buf[40];
    const char *ifk = (ev && ev->esp_netif) ? esp_netif_get_ifkey(ev->esp_netif) : "";
    switch (id) {
    case IP_EVENT_STA_LOST_IP:
        return "STA_LOST_IP";
    case IP_EVENT_ETH_LOST_IP:
        snprintf(buf, sizeof(buf), "ETH_LOST_IP %s", ifk[0] ? ifk : "?");
        return buf;
#if CONFIG_LWIP_PPP_SUPPORT
    case IP_EVENT_PPP_LOST_IP:
        return "PPP_LOST_IP";
#endif
    default:
        return "?";
    }
}

static void softap_napt_refresh(void)
{
#if CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SOFTAP && CONFIG_LWIP_IPV4_NAPT
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
    ESP_LOGD(TAG, "NAPT on SoftAP " IPSTR " (%c%c%d)", IP2STR(&ipi.ip), lw->name[0], lw->name[1],
             (int)lw->num);
#endif /* CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SOFTAP && CONFIG_LWIP_IPV4_NAPT */
}

/**
 * iot-bridge clears the global NAPT table on ETHERNET_EVENT_DISCONNECTED (bridge_eth.c).
 * SoftAP NAPT is re-applied in softap_napt_refresh(); ETH_LAN must be re-enabled the same way
 * or wired clients lose NAT after cable flap while Wi-Fi still works.
 */
static void eth_lan_napt_refresh(void)
{
#if CONFIG_LWIP_IPV4_NAPT
    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_LAN");
    if (!eth) {
        return;
    }

    esp_netif_ip_info_t ipi;
    if (esp_netif_get_ip_info(eth, &ipi) != ESP_OK) {
        return;
    }

    struct netif *lw = (struct netif *)esp_netif_get_netif_impl(eth);
    if (!lw || !netif_is_up(lw)) {
        ESP_LOGW(TAG, "ETH_LAN lwIP netif missing or down; NAPT skipped");
        return;
    }
    if (!ip_napt_enable_netif(lw, 1)) {
        ESP_LOGE(TAG, "ip_napt_enable_netif(ETH_LAN) failed");
        ip_napt_enable(ipi.ip.addr, 1);
    }
    ESP_LOGD(TAG, "NAPT on ETH_LAN " IPSTR " (%c%c%d)", IP2STR(&ipi.ip), lw->name[0], lw->name[1],
             (int)lw->num);
#endif
}

/**
 * Bridge (modem/STA/WAN) already ran esp_bridge_update_dns_info() on the LAN netifs before this
 * handler. Restarting SoftAP DHCPS here (stop + start) disconnects Wi‑Fi clients and makes Windows
 * show "no Internet" after every PPP renew/reconnect — avoid that. Only re-bind NAPT after
 * ip_napt_table_clear() on PPP loss / similar.
 */
static void lan_side_napt_only_after_uplink_ip_event(void)
{
    softap_napt_refresh();
    eth_lan_napt_refresh();
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
#if CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SOFTAP
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
    ESP_LOGI(TAG, "SoftAP DHCPS full reapply (stop/start): DNS + pool + router + NAPT");
    softap_napt_refresh();
    eth_lan_napt_refresh();
#endif
}

static esp_netif_t *get_uplink_netif(uplink_t u)
{
    if (u == UPLINK_WIFI_STA) return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (u == UPLINK_ETH_WAN) return esp_netif_get_handle_from_ifkey("ETH_WAN");
    if (u == UPLINK_USB_MODEM) return esp_netif_get_handle_from_ifkey("PPP_DEF");
    return NULL;
}

static uplink_t choose_default_uplink(void)
{
    // If both wired WAN and STA are up, prefer ETH_WAN; otherwise STA or none.
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (s_eth_up) {
        return UPLINK_ETH_WAN;
    }
#endif
    if (s_wifi_up) {
        return UPLINK_WIFI_STA;
    }
#if CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM
    if (s_ppp_up) {
        return UPLINK_USB_MODEM;
    }
#endif
    return UPLINK_NONE;
}

static void apply_default_route(uplink_t u)
{
    if (u == s_default) return;

    if (u == UPLINK_NONE) {
        s_default = u;
        ESP_LOGW(TAG, "Default route: no WAN (sta=%d eth_wan=%d ppp=%d)", (int)s_wifi_up, (int)s_eth_up,
                 (int)s_ppp_up);
        return;
    }

    esp_netif_t *netif = get_uplink_netif(u);
    if (!netif) {
        ESP_LOGW(TAG, "default uplink netif missing: want %s", uplink_t_name(u));
        return;
    }

    esp_netif_set_default_netif(netif);
    s_default = u;
    ESP_LOGI(TAG, "Default route -> %s (%s)", uplink_t_name(u), esp_netif_get_ifkey(netif));
}

static bool ifkey_is_eth_wan(const ip_event_got_ip_t *ev)
{
    return ev && ev->esp_netif && strcmp(esp_netif_get_ifkey(ev->esp_netif), "ETH_WAN") == 0;
}

static void on_uplink_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    if (id == IP_EVENT_STA_GOT_IP) {
        s_wifi_up = true;
    }
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (id == IP_EVENT_ETH_GOT_IP && ifkey_is_eth_wan(ev)) {
        s_eth_up = true;
    }
#else
    (void)ifkey_is_eth_wan;
    (void)ev;
#endif
#if CONFIG_LWIP_PPP_SUPPORT
    if (id == IP_EVENT_PPP_GOT_IP) {
        s_ppp_up = true;
    }
#endif

    // Update default route according to current policy.
    uplink_t chosen = choose_default_uplink();
    apply_default_route(chosen);

    /* Do not call softap_dhcps_full_config() here — see lan_side_napt_only_after_uplink_ip_event(). */
    lan_side_napt_only_after_uplink_ip_event();

    ESP_LOGI(TAG, "WAN IP up: %s | uplink sta=%d eth_wan=%d ppp=%d | default=%s | LAN NAPT refresh (no DHCPS restart)",
             ip_got_event_label(id, ev), (int)s_wifi_up, (int)s_eth_up, (int)s_ppp_up, uplink_t_name(chosen));

    /* Bridge / APSTA: keep Wi-Fi powersave off so coexisting STA (+ optional SoftAP) forwarding does not stall. */
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(NONE): %s", esp_err_to_name(err));
    }
}

static void on_uplink_lost_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    ip_event_got_ip_t *ev_pkt = (ip_event_got_ip_t *)data;

    if (id == IP_EVENT_STA_LOST_IP) {
        s_wifi_up = false;
    }
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (id == IP_EVENT_ETH_LOST_IP && ifkey_is_eth_wan(ev_pkt)) {
        s_eth_up = false;
    }
#endif
#if CONFIG_LWIP_PPP_SUPPORT
    if (id == IP_EVENT_PPP_LOST_IP) {
        s_ppp_up = false;
    }
#endif

    uplink_t chosen = choose_default_uplink();
    apply_default_route(chosen);
    lan_side_napt_only_after_uplink_ip_event();

    ESP_LOGW(TAG, "WAN IP lost: %s | uplink sta=%d eth_wan=%d ppp=%d | default=%s | LAN NAPT re-bound",
             ip_lost_event_label(id, ev_pkt), (int)s_wifi_up, (int)s_eth_up, (int)s_ppp_up, uplink_t_name(chosen));
}

/**
 * Link Down：bridge_eth 已清 NAPT 表；仅重绑 SoftAP NAPT，避免每次掉线都 stop/start DHCPS（会踢无线客户端）。
 */
static void eth_lan_link_down_lightweight(void)
{
    ESP_LOGI(TAG, "ETH_LAN link down → SoftAP NAPT only (no DHCPS restart)");
    softap_napt_refresh();
}

static void eth_lan_debounce_timer_cb(void *arg)
{
    (void)arg;
    if (!s_eth_lan_link_up) {
        return;
    }
    ESP_LOGI(TAG, "ETH_LAN link stable → SoftAP DHCPS full reapply (debounced)");
    softap_dhcps_full_config();
}

/**
 * ETH_LAN 插拔：Up 侧防抖后再全量 DHCPS；Down 侧仅 NAPT，减轻与 PPP / 管理 API 并发时的 lwIP 压力。
 */
static void on_eth_lan_link(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    /* Single W5500: only refresh when LAN role is up (no esp_netif_get_handle_from_eth_driver in IDF 5.2 headers here). */
    if (!esp_netif_get_handle_from_ifkey("ETH_LAN")) {
        return;
    }
    if (id != ETHERNET_EVENT_CONNECTED && id != ETHERNET_EVENT_DISCONNECTED) {
        return;
    }

    if (id == ETHERNET_EVENT_DISCONNECTED) {
        s_eth_lan_link_up = false;
        if (s_eth_lan_debounce_timer) {
            esp_timer_stop(s_eth_lan_debounce_timer);
        }
        eth_lan_link_down_lightweight();
        return;
    }

    /* ETHERNET_EVENT_CONNECTED */
    s_eth_lan_link_up = true;
    if (s_eth_lan_debounce_timer) {
        esp_timer_stop(s_eth_lan_debounce_timer);
        esp_err_t te = esp_timer_start_once(s_eth_lan_debounce_timer, ETH_LAN_LINK_DEBOUNCE_US);
        if (te != ESP_OK) {
            ESP_LOGW(TAG, "ETH_LAN debounce timer start: %s — running full reapply now", esp_err_to_name(te));
            eth_lan_debounce_timer_cb(NULL);
        } else {
            ESP_LOGI(TAG, "ETH_LAN link up → full DHCPS in %d ms if still stable",
                     (int)(ETH_LAN_LINK_DEBOUNCE_US / 1000));
        }
    } else {
        softap_dhcps_full_config();
    }
}

void system_wifi_dual_connect_init(void)
{
    const esp_timer_create_args_t eth_lan_debounce_args = {
        .callback = &eth_lan_debounce_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "eth_lan_dhcp_deb",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&eth_lan_debounce_args, &s_eth_lan_debounce_timer));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_lan_link, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &on_eth_lan_link, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_uplink_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_uplink_got_ip, NULL));
#if CONFIG_LWIP_PPP_SUPPORT
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, &on_uplink_got_ip, NULL));
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_uplink_lost_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &on_uplink_lost_ip, NULL));
#if CONFIG_LWIP_PPP_SUPPORT
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, &on_uplink_lost_ip, NULL));
#endif
#endif

    // Initial SoftAP DHCP/NAPT setup.
    softap_dhcps_full_config();
    ESP_LOGI(TAG, "init done: ETH link + WAN IP handlers; SoftAP DHCPS primed");
}

