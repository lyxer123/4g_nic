/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"

#include "system_mode_manager.h"
#include "system_usb_cat1_detect.h"
#include "system_w5500_detect.h"

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

static const system_mode_profile_t s_profiles[] = {
    {1, "4G -> Wi-Fi (SoftAP)", SYSTEM_WAN_USB_MODEM, true, false, false, false},
    {2, "4G 上行 -> W5500 内网(LAN)", SYSTEM_WAN_USB_MODEM, false, true, false, false},
    {3, "4G 上行 -> 热点 + W5500(LAN)", SYSTEM_WAN_USB_MODEM, true, true, false, false},
    {4, "Wi‑Fi STA 上行 -> 热点(LAN)", SYSTEM_WAN_WIFI_STA, true, false, true, false},
    {5, "Wi‑Fi STA 上行 -> W5500 内网(LAN)", SYSTEM_WAN_WIFI_STA, false, true, true, false},
    {6, "Wi‑Fi STA 上行 -> 热点 + W5500(LAN)", SYSTEM_WAN_WIFI_STA, true, true, true, false},
    {7, "W5500 外网(WAN) -> 热点(LAN)", SYSTEM_WAN_W5500, true, false, false, true},
    /* 单 PHY：W5500 不可同时作 WAN 与 ETH_LAN；8/9 仅占位，mode_allowed 会拒绝 */
    {8, "(无效) W5500 WAN+LAN", SYSTEM_WAN_W5500, false, true, false, true},
    {9, "(无效) W5500 WAN+热点+有线LAN", SYSTEM_WAN_W5500, true, true, false, true},
    {10, "仅配网：热点 + W5500 内网(LAN)", SYSTEM_WAN_NONE, true, true, false, false},
    {11, "仅配网：热点 (无上行)", SYSTEM_WAN_NONE, true, false, false, false},
};

static system_mode_status_t s_status = {
    .current_mode = 0,
    .target_mode = 0,
    .last_ok_mode = 0,
    .last_error = ESP_OK,
    .applying = false,
    .rollback_last_apply = false,
    .phase = "idle",
};

static esp_err_t load_work_mode(uint8_t *out)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_UI, NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    e = nvs_get_u8(h, NVS_KEY_MODE, out);
    nvs_close(h);
    return e;
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

static esp_err_t apply_sta_if_needed(bool needed)
{
    if (!needed) {
        (void)esp_wifi_disconnect();
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta) {
            (void)esp_netif_dhcpc_stop(sta);
        }
        ESP_LOGI(TAG, "STA: disconnect + dhcpc stopped (uplink is not Wi-Fi STA)");
        return ESP_OK;
    }

    char ssid[33] = {0};
    char pwd[65] = {0};
    if (load_sta_cfg(ssid, sizeof(ssid), pwd, sizeof(pwd)) != ESP_OK || ssid[0] == '\0') {
        ESP_LOGW(TAG, "STA mode requested but no saved STA credentials");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_APSTA && mode != WIFI_MODE_STA) {
        esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (e != ESP_OK) return e;
    }
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        if (!esp_netif_create_default_wifi_sta()) {
            return ESP_FAIL;
        }
    }
    wifi_config_t cfg = {0};
    size_t ssid_len = strnlen(ssid, sizeof(cfg.sta.ssid) - 1);
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    cfg.sta.ssid[ssid_len] = '\0';
    size_t pwd_len = strnlen(pwd, sizeof(cfg.sta.password) - 1);
    memcpy(cfg.sta.password, pwd, pwd_len);
    cfg.sta.password[pwd_len] = '\0';
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (e != ESP_OK) return e;
    e = esp_wifi_connect();
    ESP_LOGI(TAG, "apply STA connect: %s", esp_err_to_name(e));
    if (e == ESP_OK || e == ESP_ERR_WIFI_CONN) {
        return ESP_OK;
    }
    return e;
}

static bool str_to_ip4(const char *s, esp_ip4_addr_t *out)
{
    if (!s || s[0] == '\0') return false;
    ip4_addr_t tmp;
    if (!ip4addr_aton(s, &tmp)) return false;
    out->addr = tmp.addr;
    return true;
}

static esp_err_t apply_eth_wan_if_needed(bool needed)
{
    esp_netif_t *eth_wan = esp_netif_get_handle_from_ifkey("ETH_WAN");
    if (!eth_wan) {
        if (needed) {
            ESP_LOGW(TAG, "ETH_WAN netif missing (check SPI Ethernet + AUTO or Ext+Fwd Ethernet in sdkconfig)");
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_OK;
    }
    if (!needed) {
        /* RJ45 不作外网上行时：ETH_WAN 不得向上级要 IP（Wi-Fi/4G 作 WAN 或仅 W5500 作 LAN 时） */
        (void)esp_netif_dhcpc_stop(eth_wan);
        ESP_LOGI(TAG, "ETH_WAN: dhcpc stopped (uplink is not W5500 WAN)");
        return ESP_OK;
    }

    eth_wan_cfg_t cfg;
    load_eth_wan_cfg(&cfg);
    if (cfg.dhcp) {
        (void)esp_netif_dhcpc_stop(eth_wan);
        esp_err_t e = esp_netif_dhcpc_start(eth_wan);
        ESP_LOGI(TAG, "ETH_WAN DHCP client (mode W5500-WAN): %s", esp_err_to_name(e));
        return e;
    }

    esp_ip4_addr_t ip = {0}, gw = {0}, nm = {0};
    if (!str_to_ip4(cfg.ip, &ip) || !str_to_ip4(cfg.gw, &gw) || !str_to_ip4(cfg.mask, &nm)) {
        ESP_LOGW(TAG, "ETH_WAN static cfg missing or invalid, fallback DHCP");
        return esp_netif_dhcpc_start(eth_wan);
    }

    esp_err_t e = esp_netif_dhcpc_stop(eth_wan);
    if (e != ESP_OK) return e;
    esp_netif_ip_info_t info = {0};
    info.ip = ip;
    info.gw = gw;
    info.netmask = nm;
    e = esp_netif_set_ip_info(eth_wan, &info);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "set ETH_WAN static IP failed: %s", esp_err_to_name(e));
        return e;
    }

    esp_netif_dns_info_t dns = {0};
    if (str_to_ip4(cfg.dns1, &dns.ip.u_addr.ip4)) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        (void)esp_netif_set_dns_info(eth_wan, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (str_to_ip4(cfg.dns2, &dns.ip.u_addr.ip4)) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        (void)esp_netif_set_dns_info(eth_wan, ESP_NETIF_DNS_BACKUP, &dns);
    }
    ESP_LOGI(TAG, "ETH_WAN static config applied from NVS");
    return ESP_OK;
}

const system_mode_profile_t *system_mode_manager_get_profiles(size_t *out_count)
{
    if (out_count) {
        *out_count = sizeof(s_profiles) / sizeof(s_profiles[0]);
    }
    return s_profiles;
}

const system_mode_profile_t *system_mode_manager_get_profile(uint8_t mode)
{
    for (size_t i = 0; i < sizeof(s_profiles) / sizeof(s_profiles[0]); i++) {
        if (s_profiles[i].id == mode) {
            return &s_profiles[i];
        }
    }
    return NULL;
}

bool system_mode_manager_mode_allowed(uint8_t mode)
{
    const system_mode_profile_t *p = system_mode_manager_get_profile(mode);
    if (!p) return false;

#if !defined(CONFIG_BRIDGE_EXTERNAL_NETIF_STATION)
    if (p->needs_sta) {
        return false;
    }
#endif

#if !CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM
    if (p->wan_type == SYSTEM_WAN_USB_MODEM) {
        return false;
    }
#endif

#if defined(CONFIG_BRIDGE_DATA_FORWARDING_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if ((p->wan_type == SYSTEM_WAN_W5500 || p->lan_eth) && !system_w5500_detect_present()) {
        return false;
    }
#else
    if (p->wan_type == SYSTEM_WAN_W5500 || p->lan_eth) {
        return false;
    }
#endif

    if (p->wan_type == SYSTEM_WAN_USB_MODEM && !system_usb_cat1_detect_present()) {
        return false;
    }
    /* 一路 W5500：外网口占用 PHY 时不能再划 ETH_LAN（仅 SOFTAP / 或他口上行时可有线 LAN） */
    if (p->wan_type == SYSTEM_WAN_W5500 && p->lan_eth) {
        return false;
    }
    return true;
}

void system_mode_manager_get_status(system_mode_status_t *out)
{
    if (!out) return;
    *out = s_status;
}

esp_err_t system_mode_manager_apply(uint8_t mode)
{
    const system_mode_profile_t *target = system_mode_manager_get_profile(mode);
    if (!target) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!system_mode_manager_mode_allowed(mode)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "apply work_mode id=%u (%s)", (unsigned)mode, target->label ? target->label : "?");

    const system_mode_profile_t *prev = system_mode_manager_get_profile(s_status.current_mode);
    s_status.applying = true;
    s_status.rollback_last_apply = false;
    s_status.target_mode = mode;
    s_status.last_error = ESP_OK;
    snprintf(s_status.phase, sizeof(s_status.phase), "apply");

    esp_err_t e = apply_sta_if_needed(target->needs_sta);
    if (e == ESP_OK) {
        e = apply_eth_wan_if_needed(target->needs_eth_wan_cfg);
    }

    if (e != ESP_OK) {
        ESP_LOGW(TAG, "mode %u apply failed: %s, rollback to %u", (unsigned)mode, esp_err_to_name(e), (unsigned)s_status.current_mode);
        s_status.rollback_last_apply = true;
        snprintf(s_status.phase, sizeof(s_status.phase), "rollback");
        if (prev) {
            (void)apply_sta_if_needed(prev->needs_sta);
            (void)apply_eth_wan_if_needed(prev->needs_eth_wan_cfg);
        }
        s_status.last_error = e;
        s_status.applying = false;
        snprintf(s_status.phase, sizeof(s_status.phase), "idle");
        return e;
    }

    s_status.current_mode = mode;
    s_status.last_ok_mode = mode;
    s_status.last_error = ESP_OK;
    s_status.applying = false;
    snprintf(s_status.phase, sizeof(s_status.phase), "idle");
    ESP_LOGI(TAG, "runtime mode applied: %u", (unsigned)mode);
    return ESP_OK;
}

esp_err_t system_mode_manager_apply_saved(void)
{
    uint8_t mode = 0;
    esp_err_t e = load_work_mode(&mode);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "no valid saved mode in NVS");
        return e;
    }
    if (!system_mode_manager_get_profile(mode)) {
        ESP_LOGW(TAG, "saved mode invalid: %u", (unsigned)mode);
        return ESP_ERR_INVALID_ARG;
    }
    return system_mode_manager_apply(mode);
}

uint8_t system_mode_manager_pick_hw_default_mode(void)
{
    (void)system_usb_cat1_detect_present();
    (void)system_w5500_detect_present();

    /* Default WAN preference: W5500 > Wi-Fi STA > USB modem > AP-only provisioning. */
    const uint8_t candidates[] = {7U, 4U, 1U, 11U, 10U};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (system_mode_manager_mode_allowed(candidates[i])) {
            return candidates[i];
        }
    }
    return 11U;
}

void system_mode_manager_log_startup_plan(void)
{
    uint8_t mode = 0;
    esp_err_t e = load_work_mode(&mode);
    if (e == ESP_OK && system_mode_manager_get_profile(mode) && system_mode_manager_mode_allowed(mode)) {
        const system_mode_profile_t *p = system_mode_manager_get_profile(mode);
        ESP_LOGI(TAG, "NVS work_mode=%u — %s → will apply after netif init (e.g. W5500 WAN pulls DHCP; SoftAP=LAN)",
                 (unsigned)mode, p && p->label ? p->label : "?");
        return;
    }
    if (e == ESP_OK && system_mode_manager_get_profile(mode)) {
        ESP_LOGW(TAG, "NVS work_mode=%u invalid for this HW/build → will fall back after netif init", (unsigned)mode);
        return;
    }
    ESP_LOGI(TAG, "no work_mode in NVS → will use SoftAP provisioning (11) or HW fallback after netif init");
}

esp_err_t system_mode_manager_apply_saved_or_hw_default(void)
{
    uint8_t mode = 0;
    esp_err_t e = load_work_mode(&mode);
    if (e == ESP_OK && system_mode_manager_get_profile(mode) && system_mode_manager_mode_allowed(mode)) {
        return system_mode_manager_apply(mode);
    }
    if (e == ESP_OK && system_mode_manager_get_profile(mode)) {
        ESP_LOGW(TAG, "saved mode %u not allowed for hardware/build, using default", (unsigned)mode);
    }
    /* 无保存或不可用：优先可网页配网的纯热点，而不是「未选过就 W5500 LAN」(10) */
    if (system_mode_manager_mode_allowed(11)) {
        mode = 11U;
        ESP_LOGI(TAG, "default work_mode: 11 (SoftAP provisioning; set WAN/LAN via web after connect)");
    } else {
        mode = system_mode_manager_pick_hw_default_mode();
        ESP_LOGI(TAG, "mode 11 disallowed, HW default work mode: %u", (unsigned)mode);
    }
    return system_mode_manager_apply(mode);
}

uint8_t system_mode_manager_current(void)
{
    return s_status.current_mode;
}

