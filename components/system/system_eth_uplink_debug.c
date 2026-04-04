/*
 * Optional diagnostics: ETH_WAN DHCP state, default netif, kick dhcpc on link-up.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_eth.h"
#if CONFIG_SYSTEM_ETH_UPLINK_HTTP_CHECK
#include "esp_http_client.h"
#endif

#include "system_eth_uplink_debug.h"

static const char *TAG = "eth_uplink";

#if CONFIG_SYSTEM_ETH_UPLINK_HTTP_CHECK
static bool s_wan_http_probe_scheduled;

static bool http_probe_url(const char *url, const char *host_hdr, int *out_code, esp_err_t *out_err)
{
    /* Do not set if_name: SO_BINDTODEVICE + SPI Ethernet lwIP name has caused connect timeouts on some ICS setups. */
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .addr_type = HTTP_ADDR_TYPE_INET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        *out_err = ESP_FAIL;
        *out_code = 0;
        return false;
    }
    if (host_hdr) {
        esp_http_client_set_header(client, "Host", host_hdr);
    }
    *out_err = esp_http_client_perform(client);
    *out_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (*out_err == ESP_OK && *out_code > 0);
}

static void http_probe_task(void *arg)
{
    const char *reason = (const char *)arg;

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_netif_t *wan = esp_netif_get_handle_from_ifkey("ETH_WAN");
#if CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
    if (wan) {
        esp_netif_ip_info_t ipi = {0};
        if (esp_netif_get_ip_info(wan, &ipi) == ESP_OK) {
            esp_netif_dns_info_t dmain = {.ip = {.type = ESP_IPADDR_TYPE_V4}};
            esp_netif_dns_info_t dback = {.ip = {.type = ESP_IPADDR_TYPE_V4}};
#if CONFIG_SYSTEM_ETH_WAN_DNS_USE_GATEWAY_AS_MAIN
            dmain.ip.u_addr.ip4.addr = ipi.gw.addr;
            esp_netif_set_dns_info(wan, ESP_NETIF_DNS_MAIN, &dmain);
            if (esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_DNS_BACKUP_STR, &dback.ip.u_addr.ip4) == ESP_OK) {
                esp_netif_set_dns_info(wan, ESP_NETIF_DNS_BACKUP, &dback);
            }
#else
            if (esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_DNS_MAIN_STR, &dmain.ip.u_addr.ip4) == ESP_OK &&
                esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_DNS_BACKUP_STR, &dback.ip.u_addr.ip4) == ESP_OK) {
                esp_netif_set_dns_info(wan, ESP_NETIF_DNS_MAIN, &dmain);
                esp_netif_set_dns_info(wan, ESP_NETIF_DNS_BACKUP, &dback);
            }
#endif
        }
    }
#endif

    char url_ip[96];
    snprintf(url_ip, sizeof(url_ip), "http://" CONFIG_SYSTEM_ETH_UPLINK_CHECK_BAIDU_IP_STR "/");

    int code = 0;
    esp_err_t err = ESP_OK;
    bool ok_ip = http_probe_url(url_ip, "www.baidu.com", &code, &err);
    if (ok_ip) {
        ESP_LOGI(TAG, "WAN probe [%s]: L3+HTTP OK via baidu IP " CONFIG_SYSTEM_ETH_UPLINK_CHECK_BAIDU_IP_STR
                      " (status=%d, no DNS)",
                 reason, code);
    } else {
        ESP_LOGW(TAG, "WAN probe [%s]: baidu IP path NG url=%s err=%s status=%d", reason, url_ip,
                 esp_err_to_name(err), code);
    }

    bool ok_dns = http_probe_url("http://www.baidu.com/", NULL, &code, &err);
    if (ok_dns) {
        ESP_LOGI(TAG, "WAN probe [%s]: DNS OK (www.baidu.com status=%d)", reason, code);
    } else {
        ESP_LOGW(
            TAG,
            "WAN probe [%s]: DNS NG www.baidu.com err=%s status=%d (ICS often blocks UDP/53 to public DNS; "
            "set ETH DNS to gateway %s or fix PC firewall)",
            reason, esp_err_to_name(err), code,
#if CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
            CONFIG_SYSTEM_ETH_WAN_STATIC_GW_STR
#else
            "or use DHCP"
#endif
        );
    }

    vTaskDelete(NULL);
}

static void schedule_wan_http_probe_once(const char *reason)
{
    if (s_wan_http_probe_scheduled) {
        return;
    }
    s_wan_http_probe_scheduled = true;

    if (xTaskCreate(http_probe_task, "wan_http_probe", 6144, (void *)reason, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "WAN HTTP check: xTaskCreate failed");
    }
}
#endif /* CONFIG_SYSTEM_ETH_UPLINK_HTTP_CHECK */

static const char *dhcpc_st_str(esp_netif_dhcp_status_t s)
{
    switch (s) {
    case ESP_NETIF_DHCP_INIT: return "INIT";
    case ESP_NETIF_DHCP_STARTED: return "STARTED";
    case ESP_NETIF_DHCP_STOPPED: return "STOPPED";
    case ESP_NETIF_DHCP_STATUS_MAX: return "MAX";
    default: return "?";
    }
}

static void dump_eth_wan(const char *why)
{
    esp_netif_t *wan = esp_netif_get_handle_from_ifkey("ETH_WAN");
    if (!wan) {
        ESP_LOGW(TAG, "[%s] ETH_WAN netif missing (bridge not built with EXTERNAL_NETIF_ETHERNET?)", why);
        return;
    }

    esp_netif_dhcp_status_t dst = ESP_NETIF_DHCP_INIT;
    esp_netif_dhcpc_get_status(wan, &dst);

    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(wan, &ip);

    esp_netif_t *def = esp_netif_get_default_netif();
    const char *def_key = def ? esp_netif_get_ifkey(def) : "(null)";

    ESP_LOGI(TAG, "[%s] ETH_WAN dhcpc=%s ip=" IPSTR " mask=" IPSTR " gw=" IPSTR " default_netif=%s", why,
             dhcpc_st_str(dst), IP2STR(&ip.ip), IP2STR(&ip.netmask), IP2STR(&ip.gw), def_key);
}

#if !CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
static esp_timer_handle_t s_dhcp_defer_timer = NULL;

/* esp_timer callbacks must not block; run DHCP sequencing in a worker task. */
static void dhcp_restart_worker(void *arg)
{
    (void)arg;
    esp_netif_t *wan = esp_netif_get_handle_from_ifkey("ETH_WAN");
    if (!wan) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "deferred DHCP restart (after link PHY / lwIP settle)");
    esp_netif_dhcpc_stop(wan);
    vTaskDelay(pdMS_TO_TICKS(80));
    esp_err_t err = esp_netif_dhcpc_start(wan);
    ESP_LOGI(TAG, "esp_netif_dhcpc_start(ETH_WAN): %s", esp_err_to_name(err));
    dump_eth_wan("after_deferred_dhcp");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(wan, &ip);
    if (ip.ip.addr == 0) {
        ESP_LOGW(TAG, "still no IP after 2s - second DHCP restart");
        esp_netif_dhcpc_stop(wan);
        vTaskDelay(pdMS_TO_TICKS(80));
        err = esp_netif_dhcpc_start(wan);
        ESP_LOGI(TAG, "esp_netif_dhcpc_start (2nd): %s", esp_err_to_name(err));
        dump_eth_wan("after_2nd_dhcp_restart");
    }
    vTaskDelete(NULL);
}

static void dhcp_deferred_restart(void *arg)
{
    (void)arg;
    if (xTaskCreate(dhcp_restart_worker, "eth_dhcp_w", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(eth_dhcp_w) failed");
    }
}
#endif /* !CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP */

#if CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
static void static_ip_worker(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));

    esp_netif_t *wan = esp_netif_get_handle_from_ifkey("ETH_WAN");
    if (!wan) {
        vTaskDelete(NULL);
        return;
    }

    esp_err_t e = esp_netif_dhcpc_stop(wan);
    ESP_LOGI(TAG, "dhcpc_stop (static IP): %s", esp_err_to_name(e));

    esp_netif_ip_info_t info = {0};
    if (esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_IP_STR, &info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_GW_STR, &info.gw) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_NM_STR, &info.netmask) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid static IPv4 strings in Kconfig");
        vTaskDelete(NULL);
        return;
    }

    e = esp_netif_set_ip_info(wan, &info);
    ESP_LOGI(TAG, "ETH_WAN static IP " IPSTR " nm " IPSTR " gw " IPSTR " (%s)", IP2STR(&info.ip),
             IP2STR(&info.netmask), IP2STR(&info.gw), esp_err_to_name(e));

    esp_ip4_addr_t dns_backup = {0};
    if (esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_DNS_BACKUP_STR, &dns_backup) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid static DNS backup IPv4 string in Kconfig");
        vTaskDelete(NULL);
        return;
    }

    esp_netif_dns_info_t dns = {.ip = {.type = ESP_IPADDR_TYPE_V4}};
#if CONFIG_SYSTEM_ETH_WAN_DNS_USE_GATEWAY_AS_MAIN
    dns.ip.u_addr.ip4.addr = info.gw.addr;
    esp_netif_set_dns_info(wan, ESP_NETIF_DNS_MAIN, &dns);
    dns.ip.u_addr.ip4.addr = dns_backup.addr;
    esp_netif_set_dns_info(wan, ESP_NETIF_DNS_BACKUP, &dns);
    ESP_LOGI(TAG, "ETH_WAN static DNS MAIN=gateway " IPSTR " BACKUP=" IPSTR, IP2STR(&info.gw), IP2STR(&dns_backup));
#else
    esp_ip4_addr_t dns_main = {0};
    if (esp_netif_str_to_ip4(CONFIG_SYSTEM_ETH_WAN_STATIC_DNS_MAIN_STR, &dns_main) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid static DNS main IPv4 string in Kconfig");
        vTaskDelete(NULL);
        return;
    }
    dns.ip.u_addr.ip4.addr = dns_main.addr;
    esp_netif_set_dns_info(wan, ESP_NETIF_DNS_MAIN, &dns);
    dns.ip.u_addr.ip4.addr = dns_backup.addr;
    esp_netif_set_dns_info(wan, ESP_NETIF_DNS_BACKUP, &dns);
    ESP_LOGI(TAG, "ETH_WAN static DNS MAIN=" IPSTR " BACKUP=" IPSTR, IP2STR(&dns_main), IP2STR(&dns_backup));
#endif

    dump_eth_wan("after_static_ip");
#if CONFIG_SYSTEM_ETH_UPLINK_HTTP_CHECK
    schedule_wan_http_probe_once("static_ip");
#endif
    vTaskDelete(NULL);
}
#endif

static void on_eth_state(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == ETHERNET_EVENT_CONNECTED) {
#if CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
        ESP_LOGI(TAG, "ETHERNET_EVENT_CONNECTED - apply static IP (Kconfig)");
        if (xTaskCreate(static_ip_worker, "eth_static", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "xTaskCreate(eth_static) failed");
        }
#else
        ESP_LOGI(TAG, "ETHERNET_EVENT_CONNECTED - schedule DHCP restart in 350ms");
        if (s_dhcp_defer_timer) {
            esp_timer_stop(s_dhcp_defer_timer);
            esp_timer_start_once(s_dhcp_defer_timer, 350000);
        }
#endif
        dump_eth_wan("after_link_up_immediate");
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
#if !CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
        if (s_dhcp_defer_timer) {
            esp_timer_stop(s_dhcp_defer_timer);
        }
#endif
        ESP_LOGW(TAG, "ETHERNET_EVENT_DISCONNECTED");
        dump_eth_wan("after_link_down");
    }
}

static void on_ip_eth(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_ETH_GOT_IP) {
        return;
    }
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP on %s: " IPSTR " gw " IPSTR,
             esp_netif_get_ifkey(ev->esp_netif), IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));
    dump_eth_wan("got_ip");
#if CONFIG_SYSTEM_ETH_UPLINK_HTTP_CHECK && !CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
    if (ev->esp_netif == esp_netif_get_handle_from_ifkey("ETH_WAN")) {
        schedule_wan_http_probe_once("dhcp_got_ip");
    }
#endif
}

static void tick_timer(void *arg)
{
    (void)arg;
    dump_eth_wan("periodic");
}

void system_eth_uplink_debug_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_state, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_eth, NULL));

#if !CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP
    const esp_timer_create_args_t dhcp_defer = {
        .callback = &dhcp_deferred_restart,
        .name = "eth_dhcp_defer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&dhcp_defer, &s_dhcp_defer_timer));
#endif

    const esp_timer_create_args_t targs = {
        .callback = &tick_timer,
        .name = "eth_uplink_tick",
    };
    esp_timer_handle_t tmr = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &tmr));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tmr, 8000000));

    dump_eth_wan("init");
}
