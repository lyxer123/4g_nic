/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"

#include "system_mode_manager.h"

#define NVS_NS_UI      "bridge_ui"
#define NVS_KEY_MODE   "work_mode"

#define NVS_NS_WIFI    "wifi_cfg"
#define NVS_KEY_SSID   "sta_ssid"
#define NVS_KEY_PWD    "sta_pwd"

#define NVS_NS_ETH_WAN "eth_wan"
#define NVS_KEY_DHCP   "dhcp"
#define NVS_KEY_IP     "ip"
#define NVS_KEY_MASK   "mask"
#define NVS_KEY_GW     "gw"
#define NVS_KEY_DNS1   "dns1"
#define NVS_KEY_DNS2   "dns2"

typedef struct {
    bool dhcp;
    char ip[16];
    char mask[16];
    char gw[16];
    char dns1[16];
    char dns2[16];
} eth_wan_cfg_t;

static const char *TAG = "mode_mgr";
static uint8_t s_current_mode = 0;

static esp_err_t load_work_mode(uint8_t *out)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_UI, NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    e = nvs_get_u8(h, NVS_KEY_MODE, out);
    nvs_close(h);
    return e;
}

static bool mode_needs_sta(uint8_t mode)
{
    return mode == 4 || mode == 5 || mode == 6;
}

static bool mode_needs_eth_wan_cfg(uint8_t mode)
{
    return mode == 7;
}

static esp_err_t load_sta_cfg(char *ssid, size_t ssid_len, char *pwd, size_t pwd_len)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    e = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    if (e == ESP_OK) {
        e = nvs_get_str(h, NVS_KEY_PWD, pwd, &pwd_len);
    }
    nvs_close(h);
    return e;
}

static void eth_wan_cfg_default(eth_wan_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->dhcp = true;
}

static esp_err_t load_eth_wan_cfg(eth_wan_cfg_t *cfg)
{
    nvs_handle_t h = 0;
    eth_wan_cfg_default(cfg);
    esp_err_t e = nvs_open(NVS_NS_ETH_WAN, NVS_READONLY, &h);
    if (e != ESP_OK) return e;

    uint8_t dhcp = 1;
    if (nvs_get_u8(h, NVS_KEY_DHCP, &dhcp) == ESP_OK) {
        cfg->dhcp = dhcp != 0;
    }
    size_t n = sizeof(cfg->ip);
    if (nvs_get_str(h, NVS_KEY_IP, cfg->ip, &n) != ESP_OK) cfg->ip[0] = '\0';
    n = sizeof(cfg->mask);
    if (nvs_get_str(h, NVS_KEY_MASK, cfg->mask, &n) != ESP_OK) cfg->mask[0] = '\0';
    n = sizeof(cfg->gw);
    if (nvs_get_str(h, NVS_KEY_GW, cfg->gw, &n) != ESP_OK) cfg->gw[0] = '\0';
    n = sizeof(cfg->dns1);
    if (nvs_get_str(h, NVS_KEY_DNS1, cfg->dns1, &n) != ESP_OK) cfg->dns1[0] = '\0';
    n = sizeof(cfg->dns2);
    if (nvs_get_str(h, NVS_KEY_DNS2, cfg->dns2, &n) != ESP_OK) cfg->dns2[0] = '\0';
    nvs_close(h);
    return ESP_OK;
}

static void apply_sta_if_needed(bool needed)
{
    if (!needed) {
        esp_wifi_disconnect();
        return;
    }

    char ssid[33] = {0};
    char pwd[65] = {0};
    if (load_sta_cfg(ssid, sizeof(ssid), pwd, sizeof(pwd)) != ESP_OK || ssid[0] == '\0') {
        ESP_LOGW(TAG, "STA mode requested but no saved STA credentials");
        return;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_APSTA && mode != WIFI_MODE_STA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }
    wifi_config_t cfg = {0};
    size_t ssid_len = strnlen(ssid, sizeof(cfg.sta.ssid) - 1);
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    cfg.sta.ssid[ssid_len] = '\0';
    size_t pwd_len = strnlen(pwd, sizeof(cfg.sta.password) - 1);
    memcpy(cfg.sta.password, pwd, pwd_len);
    cfg.sta.password[pwd_len] = '\0';
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_err_t e = esp_wifi_connect();
    ESP_LOGI(TAG, "apply STA connect: %s", esp_err_to_name(e));
}

static bool str_to_ip4(const char *s, esp_ip4_addr_t *out)
{
    if (!s || s[0] == '\0') return false;
    ip4_addr_t tmp;
    if (!ip4addr_aton(s, &tmp)) return false;
    out->addr = tmp.addr;
    return true;
}

static void apply_eth_wan_if_needed(bool needed)
{
    esp_netif_t *eth_wan = esp_netif_get_handle_from_ifkey("ETH_WAN");
    if (!eth_wan) return;
    if (!needed) {
        esp_netif_dhcpc_start(eth_wan);
        return;
    }

    eth_wan_cfg_t cfg;
    load_eth_wan_cfg(&cfg);
    if (cfg.dhcp) {
        esp_netif_dhcpc_start(eth_wan);
        ESP_LOGI(TAG, "ETH_WAN use DHCP (from mode)");
        return;
    }

    esp_ip4_addr_t ip = {0}, gw = {0}, nm = {0};
    if (!str_to_ip4(cfg.ip, &ip) || !str_to_ip4(cfg.gw, &gw) || !str_to_ip4(cfg.mask, &nm)) {
        ESP_LOGW(TAG, "ETH_WAN static cfg missing or invalid, fallback DHCP");
        esp_netif_dhcpc_start(eth_wan);
        return;
    }

    esp_netif_dhcpc_stop(eth_wan);
    esp_netif_ip_info_t info = {0};
    info.ip = ip;
    info.gw = gw;
    info.netmask = nm;
    esp_err_t e = esp_netif_set_ip_info(eth_wan, &info);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "set ETH_WAN static IP failed: %s", esp_err_to_name(e));
        return;
    }

    esp_netif_dns_info_t dns = {0};
    if (str_to_ip4(cfg.dns1, &dns.ip.u_addr.ip4)) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(eth_wan, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (str_to_ip4(cfg.dns2, &dns.ip.u_addr.ip4)) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(eth_wan, ESP_NETIF_DNS_BACKUP, &dns);
    }
    ESP_LOGI(TAG, "ETH_WAN static config applied from NVS");
}

esp_err_t system_mode_manager_apply(uint8_t mode)
{
    if (mode < 1 || mode > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    apply_sta_if_needed(mode_needs_sta(mode));
    apply_eth_wan_if_needed(mode_needs_eth_wan_cfg(mode));
    s_current_mode = mode;
    ESP_LOGI(TAG, "runtime mode applied: %u", (unsigned)mode);
    return ESP_OK;
}

esp_err_t system_mode_manager_apply_saved(void)
{
    uint8_t mode = 0;
    esp_err_t e = load_work_mode(&mode);
    if (e != ESP_OK || mode < 1 || mode > 7) {
        ESP_LOGW(TAG, "no valid saved mode in NVS");
        return e == ESP_OK ? ESP_ERR_NOT_FOUND : e;
    }
    return system_mode_manager_apply(mode);
}

uint8_t system_mode_manager_current(void)
{
    return s_current_mode;
}

