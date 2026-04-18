#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_bridge.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_w5500_detect.h"
#include "system_usb_cat1_detect.h"
#include "system_mode_manager.h"
#include "web_service.h"

#define LOG_RING_LINES 40
#define LOG_LINE_MAX 128

#define WEB_UI_FW_VERSION "4G_NIC " __DATE__ " " __TIME__

static const char *TAG = "web_service";
static httpd_handle_t s_server = NULL;
static bool s_running = false;

static char s_log_ring[LOG_RING_LINES][LOG_LINE_MAX];
static int s_log_ring_pos;
static int s_log_ring_count;

static void log_ring_append(const char *line)
{
    if (!line) {
        return;
    }
    char *dst = s_log_ring[s_log_ring_pos];
    snprintf(dst, LOG_LINE_MAX, "%s", line);
    s_log_ring_pos = (s_log_ring_pos + 1) % LOG_RING_LINES;
    if (s_log_ring_count < LOG_RING_LINES) {
        s_log_ring_count++;
    }
}

static void log_ring_fmt(const char *fmt, ...)
{
    char buf[LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_ring_append(buf);
}

#define NVS_NS_UI      "bridge_ui"
#define NVS_KEY_MODE   "work_mode"
#define NVS_NS_ETH_WAN "eth_wan"
#define NVS_KEY_DHCP   "dhcp"
#define NVS_KEY_IP     "ip"
#define NVS_KEY_MASK   "mask"
#define NVS_KEY_GW     "gw"
#define NVS_KEY_DNS1   "dns1"
#define NVS_KEY_DNS2   "dns2"

#define NVS_NS_WEBUI    "web_ui"
#define NVS_NS_LANUI    "lan_ui"
#define NVS_KEY_PROBE1  "probe1"
#define NVS_KEY_PROBE2  "probe2"
#define NVS_KEY_PROBE3  "probe3"
#define NVS_KEY_PROBE4  "probe4"
#define NVS_KEY_TZ      "timezone"
#define NVS_KEY_ADMIN   "admin_pwd"
#define NVS_KEY_APUI    "ap_ui_json"
#define NVS_KEY_WAN4G   "wan_4g"
#define NVS_KEY_WANNM   "wan_net_mode"
#define NVS_KEY_UIWM    "ui_work_mode"
#define NVS_KEY_WANTYPE "wan_type"
#define NVS_KEY_SCH_EN  "rb_sch_en"
#define NVS_KEY_SCH_H   "rb_sch_h"
#define NVS_KEY_SCH_M   "rb_sch_m"
#define NVS_KEY_TRAFFIC "traffic_en"
#define NVS_KEY_APN     "apn_str"
#define NVS_KEY_APNUSER "apn_user"
#define NVS_KEY_APNPWD  "apn_pwd"

static const char *tz_display_to_posix(const char *tz_display)
{
    if (!tz_display || tz_display[0] == '\0') {
        return "CST-8";
    }
    if (strstr(tz_display, "Asia/Shanghai") || strstr(tz_display, "GMT+08")) {
        return "CST-8";
    }
    if (strstr(tz_display, "UTC")) {
        return "UTC0";
    }
    /* Fallback: keep previous behavior for custom user-entered values */
    return tz_display;
}

static void apply_timezone_from_nvs(void)
{
    char tz[64] = "(GMT+08:00) Asia/Shanghai";
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(tz);
        if (nvs_get_str(h, NVS_KEY_TZ, tz, &n) != ESP_OK) {
            strlcpy(tz, "(GMT+08:00) Asia/Shanghai", sizeof(tz));
        }
        nvs_close(h);
    }
    setenv("TZ", tz_display_to_posix(tz), 1);
    tzset();
}

typedef struct {
    bool dhcp;
    char ip[16];
    char mask[16];
    char gw[16];
    char dns1[16];
    char dns2[16];
} eth_wan_cfg_t;

static esp_err_t load_work_mode_u8(uint8_t *out)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_UI, NVS_READONLY, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_get_u8(h, NVS_KEY_MODE, out);
    nvs_close(h);
    return e;
}

static esp_err_t save_work_mode_u8(uint8_t v)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_UI, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_u8(h, NVS_KEY_MODE, v);
    if (e == ESP_OK) {
        e = nvs_commit(h);
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
    if (e != ESP_OK) {
        return e;
    }

    uint8_t dhcp = 1;
    if (nvs_get_u8(h, NVS_KEY_DHCP, &dhcp) == ESP_OK) {
        cfg->dhcp = (dhcp != 0);
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

static esp_err_t save_eth_wan_cfg(const eth_wan_cfg_t *cfg)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_ETH_WAN, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_u8(h, NVS_KEY_DHCP, cfg->dhcp ? 1 : 0);
    if (e == ESP_OK) e = nvs_set_str(h, NVS_KEY_IP, cfg->ip);
    if (e == ESP_OK) e = nvs_set_str(h, NVS_KEY_MASK, cfg->mask);
    if (e == ESP_OK) e = nvs_set_str(h, NVS_KEY_GW, cfg->gw);
    if (e == ESP_OK) e = nvs_set_str(h, NVS_KEY_DNS1, cfg->dns1);
    if (e == ESP_OK) e = nvs_set_str(h, NVS_KEY_DNS2, cfg->dns2);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

static esp_err_t clear_eth_wan_cfg(void)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_ETH_WAN, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    nvs_erase_key(h, NVS_KEY_DHCP);
    nvs_erase_key(h, NVS_KEY_IP);
    nvs_erase_key(h, NVS_KEY_MASK);
    nvs_erase_key(h, NVS_KEY_GW);
    nvs_erase_key(h, NVS_KEY_DNS1);
    nvs_erase_key(h, NVS_KEY_DNS2);
    e = nvs_commit(h);
    nvs_close(h);
    return e;
}

static bool ip_text_valid(const char *v)
{
    if (!v) return false;
    if (v[0] == '\0') return true;
    int a, b, c, d;
    char tail = 0;
    if (sscanf(v, "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) != 4) return false;
    return a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 && d >= 0 && d <= 255;
}

/** Boot-time USB enumeration matched a known Cat1/modem (independent of menuconfig WAN role). */
static bool hw_usb_modem_probe(void)
{
#if CONFIG_SYSTEM_USB_CAT1_DETECT
    return system_usb_cat1_detect_present();
#else
    return false;
#endif
}

/** USB modem presence from boot-time probe; UI modes are hardware-driven. */
static bool hw_usb_lte(void)
{
    return hw_usb_modem_probe();
}

static bool hw_w5500(void)
{
    return system_w5500_detect_present();
}

static void mode_push(cJSON *arr, const system_mode_profile_t *p)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", p->id);
    cJSON_AddStringToObject(o, "label", p->label);
    cJSON_AddBoolToObject(o, "needs_sta", p->needs_sta);
    cJSON_AddBoolToObject(o, "needs_eth_wan", p->needs_eth_wan_cfg);
    cJSON_AddNumberToObject(o, "wan_type", (double)p->wan_type);
    cJSON_AddBoolToObject(o, "lan_softap", p->lan_softap);
    cJSON_AddBoolToObject(o, "lan_eth", p->lan_eth);
    cJSON_AddItemToArray(arr, o);
}

static cJSON *json_build_mode_list(void)
{
    cJSON *arr = cJSON_CreateArray();
    size_t n = 0;
    const system_mode_profile_t *profiles = system_mode_manager_get_profiles(&n);
    for (size_t i = 0; i < n; i++) {
        if (system_mode_manager_mode_allowed(profiles[i].id)) {
            mode_push(arr, &profiles[i]);
        }
    }
    return arr;
}

static bool netif_has_ipv4(const char *ifkey)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey(ifkey);
    if (!n) {
        return false;
    }
    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(n, &ip) != ESP_OK) {
        return false;
    }
    return ip.ip.addr != 0;
}

static bool mode_allowed_for_wan_type(system_wan_type_t wan_type)
{
    size_t n = 0;
    const system_mode_profile_t *profiles = system_mode_manager_get_profiles(&n);
    for (size_t i = 0; i < n; i++) {
        if (profiles[i].wan_type == wan_type && profiles[i].lan_softap
            && system_mode_manager_mode_allowed(profiles[i].id)) {
            return true;
        }
    }
    return false;
}

static uint8_t pick_preferred_mode_for_wan_type(system_wan_type_t wan_type)
{
    size_t n = 0;
    const system_mode_profile_t *profiles = system_mode_manager_get_profiles(&n);
    for (size_t i = 0; i < n; i++) {
        if (profiles[i].wan_type == wan_type && profiles[i].lan_softap
            && system_mode_manager_mode_allowed(profiles[i].id)) {
            return profiles[i].id;
        }
    }
    return 0;
}

static const char *wan_unavailable_reason(system_wan_type_t wan_type)
{
    switch (wan_type) {
    case SYSTEM_WAN_NONE:
        return mode_allowed_for_wan_type(wan_type) ? "ok" : "no_ap_profile";
    case SYSTEM_WAN_USB_MODEM:
#if CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM
        if (!hw_usb_lte()) {
            return "usb_modem_absent";
        }
        return netif_has_ipv4("PPP_DEF") ? "ok" : "usb_present_no_ip";
#else
        return "feature_disabled";
#endif
    case SYSTEM_WAN_WIFI_STA:
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_STATION)
        return mode_allowed_for_wan_type(wan_type) ? "ok" : "no_sta_softap_profile";
#else
        return "feature_disabled";
#endif
    case SYSTEM_WAN_W5500:
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
        return hw_w5500() ? "ok" : "w5500_absent";
#else
        return "feature_disabled";
#endif
    default:
        return "unknown";
    }
}

static cJSON *json_build_wan_options(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }
    const system_wan_type_t all_types[] = {SYSTEM_WAN_WIFI_STA, SYSTEM_WAN_W5500, SYSTEM_WAN_USB_MODEM, SYSTEM_WAN_NONE};
    for (size_t i = 0; i < sizeof(all_types) / sizeof(all_types[0]); i++) {
        const system_wan_type_t wt = all_types[i];
        const char *reason = wan_unavailable_reason(wt);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "wan_type", (double)wt);
        cJSON_AddBoolToObject(o, "available", strcmp(reason, "ok") == 0);
        cJSON_AddStringToObject(o, "reason_code", reason);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static void json_add_netif_ip(cJSON *root, const char *name, const char *ifkey)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey(ifkey);
    if (!n) {
        cJSON_AddNullToObject(root, name);
        return;
    }
    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(n, &ip) != ESP_OK) {
        cJSON_AddNullToObject(root, name);
        return;
    }
    char buf[20];
    esp_ip4addr_ntoa(&ip.ip, buf, sizeof(buf));
    cJSON_AddStringToObject(root, name, buf);
}

static const char *content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".js")) return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".jpg") || strstr(path, ".jpeg")) return "image/jpeg";
    if (strstr(path, ".svg")) return "image/svg+xml";
    return "text/plain";
}

static esp_err_t send_json(httpd_req_t *req, int code, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, code == 200 ? "200 OK" : "400 Bad Request");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t save_sta_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open("wifi_cfg", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_str(nvs, "sta_ssid", ssid);
    if (ret != ESP_OK) goto err;
    ret = nvs_set_str(nvs, "sta_pwd", password);
    if (ret != ESP_OK) goto err;
    ret = nvs_commit(nvs);
    if (ret != ESP_OK) goto err;
    nvs_close(nvs);
    return ESP_OK;
err:
    nvs_close(nvs);
    return ret;
}

static esp_err_t clear_sta_from_nvs(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open("wifi_cfg", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Erase keys; if keys don't exist treat it as success. */
    esp_err_t r1 = nvs_erase_key(nvs, "sta_ssid");
    esp_err_t r2 = nvs_erase_key(nvs, "sta_pwd");
    esp_err_t r = ESP_OK;
    if (r1 != ESP_OK && r1 != ESP_ERR_NVS_NOT_FOUND) r = r1;
    if (r2 != ESP_OK && r2 != ESP_ERR_NVS_NOT_FOUND) r = r2;

    if (r == ESP_OK) {
        r = nvs_commit(nvs);
    } else {
        /* Still try to commit so erasures take effect even if one key missing. */
        nvs_commit(nvs);
    }

    nvs_close(nvs);
    return r;
}

static esp_err_t apply_sta_config(const char *ssid, const char *password)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    ESP_RETURN_ON_ERROR(esp_wifi_get_mode(&mode), TAG, "get wifi mode failed");

    /* SoftAP-only bridge leaves WiFi in AP mode; STA config requires APSTA (or STA). */
    if (mode == WIFI_MODE_AP) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA failed");
    } else if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA failed");
    }

    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        if (!esp_netif_create_default_wifi_sta()) {
            return ESP_ERR_NO_MEM;
        }
    }

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    /* Honor WPA2/3 transition APs (log may show WPA3-SAE while threshold was WPA2-only). */
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set sta config failed");
    return esp_wifi_connect();
}

static esp_err_t uri_static_get(httpd_req_t *req)
{
    char req_path[256] = {0};
    char file_path[320] = {0};
    char gz_path[336] = {0};
    struct stat st;

    const char *uri = req->uri[0] ? req->uri : "/";
    strlcpy(req_path, uri, sizeof(req_path));
    char *q = strchr(req_path, '?');
    if (q) *q = '\0';
    if (strcmp(req_path, "/") == 0) strlcpy(req_path, "/index.html", sizeof(req_path));

    snprintf(file_path, sizeof(file_path), "/www%s", req_path);
    snprintf(gz_path, sizeof(gz_path), "%s.gz", file_path);

    bool use_gz = (stat(gz_path, &st) == 0);
    if (!use_gz && stat(file_path, &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    const char *open_path = use_gz ? gz_path : file_path;
    FILE *f = fopen(open_path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Open failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type(file_path));
    if (use_gz) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    /* Keep chunk small: httpd task default stack is tight; stack_size raised below too. */
    char chunk[512];
    size_t n = 0;
    do {
        n = fread(chunk, 1, sizeof(chunk), f);
        if (n > 0 && httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    } while (n > 0);

    fclose(f);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t uri_wifi_get(httpd_req_t *req)
{
    wifi_config_t cfg = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &cfg), TAG, "read wifi config failed");

    char resp[256];
    snprintf(resp, sizeof(resp), "{\"ssid\":\"%s\"}", cfg.sta.ssid);
    return send_json(req, 200, resp);
}

static esp_err_t uri_wifi_post(httpd_req_t *req)
{
    char body[256] = {0};
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"recv failed\"}");
    }
    body[r] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(pwd) || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"ssid/password required\"}");
    }

    esp_err_t ret = save_sta_to_nvs(ssid->valuestring, pwd->valuestring);
    if (ret == ESP_OK) {
        ret = apply_sta_config(ssid->valuestring, pwd->valuestring);
    }
    cJSON_Delete(root);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "save/apply sta failed: %s", esp_err_to_name(ret));
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"save/apply failed\"}");
    }
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_wifi_clear_post(httpd_req_t *req)
{
    (void)req;
    esp_err_t ret = clear_sta_from_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "clear sta from nvs failed: %s", esp_err_to_name(ret));
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"clear failed\"}");
    }

    /* Stop station; keep WIFI_STA_DEF when IoT-Bridge created it at boot (destroy breaks
     * bridge netif list + duplicate handlers if user reconnects later). */
    esp_wifi_disconnect();
    esp_err_t werr = esp_wifi_set_mode(WIFI_MODE_AP);
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "wifi_set_mode(AP) after clear: %s", esp_err_to_name(werr));
    }

    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_wifi_scan_get(httpd_req_t *req)
{
    const int max_aps = 20;
    const size_t json_cap = 3072;

    /* Some modes may leave Wi-Fi in AP-only mode; scan requires STA capability. */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"wifi not ready\"}");
    }
    if (mode == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"scan start failed\"}");
        }
    } else if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"scan start failed\"}");
        }
    }
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        if (!esp_netif_create_default_wifi_sta()) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"sta netif create failed\"}");
        }
    }

    wifi_scan_config_t scan_cfg = {0};
    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"scan start failed\"}");
    }

    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"scan ap_num failed\"}");
    }
    if (ap_num > max_aps) {
        ap_num = max_aps;
    }

    if (ap_num == 0) {
        return send_json(req, 200, "{\"aps\":[]}");
    }

    wifi_ap_record_t *recs = calloc(ap_num, sizeof(wifi_ap_record_t));
    char *resp = malloc(json_cap);
    if (!recs || !resp) {
        free(recs);
        free(resp);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }

    uint16_t count = ap_num;

    err = esp_wifi_scan_get_ap_records(&count, recs);
    if (err != ESP_OK) {
        free(recs);
        free(resp);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"scan records failed\"}");
    }

    size_t used = 0;
    used += snprintf(resp + used, json_cap - used, "{\"aps\":[");
    for (int i = 0; i < count && used < json_cap - 80; i++) {
        used += snprintf(resp + used, json_cap - used,
            "%s{\"ssid\":\"%s\",\"rssi\":%d}", i ? "," : "", recs[i].ssid, recs[i].rssi);
    }
    snprintf(resp + used, json_cap - used, "]}");
    err = send_json(req, 200, resp);
    free(recs);
    free(resp);
    return err;
}

static esp_err_t uri_eth_wan_get(httpd_req_t *req)
{
    eth_wan_cfg_t cfg;
    load_eth_wan_cfg(&cfg);
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"dhcp\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\",\"dns1\":\"%s\",\"dns2\":\"%s\"}",
             cfg.dhcp ? "true" : "false", cfg.ip, cfg.mask, cfg.gw, cfg.dns1, cfg.dns2);
    return send_json(req, 200, resp);
}

static esp_err_t uri_eth_wan_post(httpd_req_t *req)
{
    char body[320] = {0};
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"recv failed\"}");
    }
    body[r] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }

    cJSON *dhcp = cJSON_GetObjectItem(root, "dhcp");
    cJSON *ip = cJSON_GetObjectItem(root, "ip");
    cJSON *mask = cJSON_GetObjectItem(root, "mask");
    cJSON *gw = cJSON_GetObjectItem(root, "gw");
    cJSON *dns1 = cJSON_GetObjectItem(root, "dns1");
    cJSON *dns2 = cJSON_GetObjectItem(root, "dns2");
    if (!cJSON_IsBool(dhcp) || !cJSON_IsString(ip) || !cJSON_IsString(mask)
        || !cJSON_IsString(gw) || !cJSON_IsString(dns1) || !cJSON_IsString(dns2)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"dhcp/ip/mask/gw/dns1/dns2 required\"}");
    }
    if (!ip_text_valid(ip->valuestring) || !ip_text_valid(mask->valuestring)
        || !ip_text_valid(gw->valuestring) || !ip_text_valid(dns1->valuestring)
        || !ip_text_valid(dns2->valuestring)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid ipv4 text\"}");
    }

    eth_wan_cfg_t cfg;
    eth_wan_cfg_default(&cfg);
    cfg.dhcp = cJSON_IsTrue(dhcp);
    snprintf(cfg.ip, sizeof(cfg.ip), "%s", ip->valuestring);
    snprintf(cfg.mask, sizeof(cfg.mask), "%s", mask->valuestring);
    snprintf(cfg.gw, sizeof(cfg.gw), "%s", gw->valuestring);
    snprintf(cfg.dns1, sizeof(cfg.dns1), "%s", dns1->valuestring);
    snprintf(cfg.dns2, sizeof(cfg.dns2), "%s", dns2->valuestring);
    cJSON_Delete(root);

    esp_err_t e = save_eth_wan_cfg(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "save eth wan cfg failed: %s", esp_err_to_name(e));
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs save failed\"}");
    }
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_eth_wan_clear_post(httpd_req_t *req)
{
    (void)req;
    esp_err_t e = clear_eth_wan_cfg();
    if (e != ESP_OK) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"clear failed\"}");
    }
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_system_hw_get(httpd_req_t *req)
{
    uint16_t vid = 0;
    uint16_t pid = 0;
    system_usb_cat1_detect_last_ids(&vid, &pid);
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"w5500\":%s,\"usb_modem_present\":%s,\"usb_lte\":%s,\"usb_vid\":\"0x%04x\",\"usb_pid\":\"0x%04x\"}",
             hw_w5500() ? "true" : "false",
             hw_usb_modem_probe() ? "true" : "false",
             hw_usb_lte() ? "true" : "false",
             (unsigned)vid, (unsigned)pid);
    return send_json(req, 200, buf);
}

static esp_err_t uri_system_overview_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }

    wifi_mode_t wm = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&wm) == ESP_OK) {
        const char *ms = "null";
        if (wm == WIFI_MODE_STA) {
            ms = "STA";
        } else if (wm == WIFI_MODE_AP) {
            ms = "AP";
        } else if (wm == WIFI_MODE_APSTA) {
            ms = "APSTA";
        }
        cJSON_AddStringToObject(root, "wifi_mode", ms);
    } else {
        cJSON_AddNullToObject(root, "wifi_mode");
    }

    json_add_netif_ip(root, "ip_softap", "WIFI_AP_DEF");
    json_add_netif_ip(root, "ip_sta", "WIFI_STA_DEF");
    json_add_netif_ip(root, "ip_eth_lan", "ETH_LAN");
    json_add_netif_ip(root, "ip_eth_wan", "ETH_WAN");
    json_add_netif_ip(root, "ip_ppp", "PPP_DEF");

    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000LL));

    cJSON_AddBoolToObject(root, "hw_w5500", hw_w5500());
    cJSON_AddBoolToObject(root, "hw_usb_modem_present", hw_usb_modem_probe());
    cJSON_AddBoolToObject(root, "hw_usb_lte", hw_usb_lte());

    eth_wan_cfg_t wan_cfg;
    load_eth_wan_cfg(&wan_cfg);
    cJSON *saved_eth_wan = cJSON_CreateObject();
    if (saved_eth_wan) {
        cJSON_AddBoolToObject(saved_eth_wan, "dhcp", wan_cfg.dhcp);
        cJSON_AddStringToObject(saved_eth_wan, "ip", wan_cfg.ip);
        cJSON_AddStringToObject(saved_eth_wan, "mask", wan_cfg.mask);
        cJSON_AddStringToObject(saved_eth_wan, "gw", wan_cfg.gw);
        cJSON_AddStringToObject(saved_eth_wan, "dns1", wan_cfg.dns1);
        cJSON_AddStringToObject(saved_eth_wan, "dns2", wan_cfg.dns2);
        cJSON_AddItemToObject(root, "saved_eth_wan", saved_eth_wan);
    }

    uint8_t mode = 0;
    if (load_work_mode_u8(&mode) == ESP_OK && system_mode_manager_get_profile(mode)) {
        cJSON_AddNumberToObject(root, "saved_work_mode", mode);
        const system_mode_profile_t *sp = system_mode_manager_get_profile(mode);
        if (sp) {
            cJSON_AddStringToObject(root, "saved_work_mode_label", sp->label);
        }
    } else {
        cJSON_AddNullToObject(root, "saved_work_mode");
    }

    system_mode_status_t mst;
    system_mode_manager_get_status(&mst);
    cJSON *mstj = cJSON_CreateObject();
    if (mstj) {
        cJSON_AddNumberToObject(mstj, "current_mode", mst.current_mode);
        cJSON_AddNumberToObject(mstj, "target_mode", mst.target_mode);
        cJSON_AddNumberToObject(mstj, "last_ok_mode", mst.last_ok_mode);
        cJSON_AddBoolToObject(mstj, "applying", mst.applying);
        cJSON_AddBoolToObject(mstj, "rollback_last_apply", mst.rollback_last_apply);
        cJSON_AddNumberToObject(mstj, "last_error", (double)mst.last_error);
        cJSON_AddStringToObject(mstj, "phase", mst.phase);
        cJSON_AddItemToObject(root, "mode_runtime", mstj);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_mode_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }

    uint8_t cur = 0;
    esp_err_t le = load_work_mode_u8(&cur);
    if (le == ESP_OK && system_mode_manager_get_profile(cur)) {
        cJSON_AddNumberToObject(root, "current", cur);
    } else {
        cJSON_AddNumberToObject(root, "current", 0);
    }

    cJSON *hw = cJSON_CreateObject();
    if (!hw) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    cJSON_AddBoolToObject(hw, "w5500", hw_w5500());
    cJSON_AddBoolToObject(hw, "usb_modem_present", hw_usb_modem_probe());
    cJSON_AddBoolToObject(hw, "usb_lte", hw_usb_lte());
    uint16_t vid = 0;
    uint16_t pid = 0;
    system_usb_cat1_detect_last_ids(&vid, &pid);
    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "0x%04x:0x%04x", (unsigned)vid, (unsigned)pid);
    cJSON_AddStringToObject(hw, "usb_ids", idbuf);
    cJSON_AddItemToObject(root, "hardware", hw);

    cJSON *modes = json_build_mode_list();
    if (!modes) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    cJSON_AddItemToObject(root, "modes", modes);
    cJSON *wans = json_build_wan_options();
    if (wans) {
        cJSON_AddItemToObject(root, "wan_options", wans);
    }

    system_mode_status_t mst;
    system_mode_manager_get_status(&mst);
    cJSON *mstj = cJSON_CreateObject();
    if (mstj) {
        cJSON_AddNumberToObject(mstj, "current_mode", mst.current_mode);
        cJSON_AddNumberToObject(mstj, "target_mode", mst.target_mode);
        cJSON_AddNumberToObject(mstj, "last_ok_mode", mst.last_ok_mode);
        cJSON_AddBoolToObject(mstj, "applying", mst.applying);
        cJSON_AddBoolToObject(mstj, "rollback_last_apply", mst.rollback_last_apply);
        cJSON_AddNumberToObject(mstj, "last_error", (double)mst.last_error);
        cJSON_AddStringToObject(mstj, "phase", mst.phase);
        cJSON_AddItemToObject(root, "runtime_status", mstj);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_mode_post(httpd_req_t *req)
{
    char body[128] = {0};
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"recv failed\"}");
    }
    body[r] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *mid = cJSON_GetObjectItem(root, "mode");
    if (!cJSON_IsNumber(mid)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"mode required\"}");
    }
    uint8_t m = (uint8_t)mid->valuedouble;
    cJSON_Delete(root);

    if (!system_mode_manager_get_profile(m)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"unknown mode\"}");
    }
    if (!system_mode_manager_mode_allowed(m)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"mode not allowed for this hardware\"}");
    }

    esp_err_t ret = save_work_mode_u8(m);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "save work_mode: %s", esp_err_to_name(ret));
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs save failed\"}");
    }

    ret = system_mode_manager_apply(m);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mode saved but apply failed: %s", esp_err_to_name(ret));
        return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"Saved to NVS. Apply failed now; reboot will re-apply.\"}");
    }
    return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"Saved to NVS and applied at runtime.\"}");
}

static esp_err_t uri_mode_apply_post(httpd_req_t *req)
{
    char body[128] = {0};
    uint8_t m = 0;
    if (req->content_len > 0) {
        int len = req->content_len;
        if (len >= (int)sizeof(body)) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
        }
        int r = httpd_req_recv(req, body, len);
        if (r <= 0) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"recv failed\"}");
        }
        body[r] = '\0';
        cJSON *root = cJSON_Parse(body);
        if (!root) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
        }
        cJSON *mid = cJSON_GetObjectItem(root, "mode");
        const bool have_mode = cJSON_IsNumber(mid);
        if (have_mode) {
            m = (uint8_t)mid->valuedouble;
        }
        cJSON_Delete(root);
        if (!have_mode && load_work_mode_u8(&m) != ESP_OK) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"mode or saved work_mode required\"}");
        }
    } else {
        if (load_work_mode_u8(&m) != ESP_OK) {
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"no saved mode\"}");
        }
    }

    if (!system_mode_manager_get_profile(m)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"unknown mode\"}");
    }
    esp_err_t e = system_mode_manager_apply(m);
    if (e != ESP_OK) {
        char out[160];
        snprintf(out, sizeof(out), "{\"status\":\"error\",\"message\":\"apply failed: %s\"}", esp_err_to_name(e));
        return send_json(req, 400, out);
    }
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_mode_status_get(httpd_req_t *req)
{
    system_mode_status_t st = {0};
    system_mode_manager_get_status(&st);
    char out[320];
    snprintf(out, sizeof(out),
             "{\"current_mode\":%u,\"target_mode\":%u,\"last_ok_mode\":%u,"
             "\"applying\":%s,\"rollback_last_apply\":%s,\"last_error\":%d,\"phase\":\"%s\"}",
             (unsigned)st.current_mode, (unsigned)st.target_mode, (unsigned)st.last_ok_mode,
             st.applying ? "true" : "false",
             st.rollback_last_apply ? "true" : "false",
             (int)st.last_error, st.phase);
    return send_json(req, 200, out);
}

typedef struct {
    char ip[16];
    char mask[16];
    bool dhcp;
    char dhcp_start[16];
    char dhcp_end[16];
    int lease_hours;
    char dns1[16];
    char dns2[16];
} lan_ui_cfg_t;

static void lan_ui_default(lan_ui_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->ip, "192.168.0.1", sizeof(c->ip));
    strlcpy(c->mask, "255.255.0.0", sizeof(c->mask));
    c->dhcp = true;
    strlcpy(c->dhcp_start, "192.168.0.100", sizeof(c->dhcp_start));
    strlcpy(c->dhcp_end, "192.168.8.99", sizeof(c->dhcp_end));
    c->lease_hours = 12;
    strlcpy(c->dns1, "8.8.8.8", sizeof(c->dns1));
    strlcpy(c->dns2, "114.114.114.114", sizeof(c->dns2));
}

static esp_err_t load_lan_ui(lan_ui_cfg_t *c)
{
    lan_ui_default(c);
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_LANUI, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;
    }
    size_t n = sizeof(c->ip);
    if (nvs_get_str(h, "ip", c->ip, &n) != ESP_OK) {
        strlcpy(c->ip, "192.168.0.1", sizeof(c->ip));
    }
    n = sizeof(c->mask);
    if (nvs_get_str(h, "mask", c->mask, &n) != ESP_OK) {
        strlcpy(c->mask, "255.255.0.0", sizeof(c->mask));
    }
    uint8_t d = 1;
    if (nvs_get_u8(h, "dhcp", &d) == ESP_OK) {
        c->dhcp = (d != 0);
    }
    n = sizeof(c->dhcp_start);
    if (nvs_get_str(h, "dstart", c->dhcp_start, &n) != ESP_OK) {
        strlcpy(c->dhcp_start, "192.168.0.100", sizeof(c->dhcp_start));
    }
    n = sizeof(c->dhcp_end);
    if (nvs_get_str(h, "dend", c->dhcp_end, &n) != ESP_OK) {
        strlcpy(c->dhcp_end, "192.168.8.99", sizeof(c->dhcp_end));
    }
    int32_t lh = 12;
    if (nvs_get_i32(h, "lease_h", &lh) == ESP_OK) {
        c->lease_hours = (int)lh;
    }
    n = sizeof(c->dns1);
    if (nvs_get_str(h, "dns1", c->dns1, &n) != ESP_OK) {
        strlcpy(c->dns1, "8.8.8.8", sizeof(c->dns1));
    }
    n = sizeof(c->dns2);
    if (nvs_get_str(h, "dns2", c->dns2, &n) != ESP_OK) {
        strlcpy(c->dns2, "114.114.114.114", sizeof(c->dns2));
    }
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t save_lan_ui(const lan_ui_cfg_t *c)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(NVS_NS_LANUI, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_str(h, "ip", c->ip);
    if (e == ESP_OK) {
        e = nvs_set_str(h, "mask", c->mask);
    }
    if (e == ESP_OK) {
        e = nvs_set_u8(h, "dhcp", c->dhcp ? 1u : 0u);
    }
    if (e == ESP_OK) {
        e = nvs_set_str(h, "dstart", c->dhcp_start);
    }
    if (e == ESP_OK) {
        e = nvs_set_str(h, "dend", c->dhcp_end);
    }
    if (e == ESP_OK) {
        e = nvs_set_i32(h, "lease_h", c->lease_hours);
    }
    if (e == ESP_OK) {
        e = nvs_set_str(h, "dns1", c->dns1);
    }
    if (e == ESP_OK) {
        e = nvs_set_str(h, "dns2", c->dns2);
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

static char *http_recv_body(httpd_req_t *req, int max_len)
{
    int len = req->content_len;
    if (len <= 0 || len > max_len) {
        return NULL;
    }
    char *buf = malloc((size_t)len + 1u);
    if (!buf) {
        return NULL;
    }
    int r = httpd_req_recv(req, buf, len);
    if (r <= 0) {
        free(buf);
        return NULL;
    }
    buf[r] = '\0';
    return buf;
}

static uint8_t pick_mode_for_ui_working_mode(const char *wm)
{
    if (!wm) {
        return system_mode_manager_pick_hw_default_mode();
    }
    if (strcmp(wm, "router") == 0) {
        const system_wan_type_t pref[] = {SYSTEM_WAN_W5500, SYSTEM_WAN_WIFI_STA, SYSTEM_WAN_USB_MODEM};
        for (size_t i = 0; i < sizeof(pref) / sizeof(pref[0]); i++) {
            uint8_t m = pick_preferred_mode_for_wan_type(pref[i]);
            if (m != 0) {
                return m;
            }
        }
    } else if (strcmp(wm, "ap") == 0) {
        if (system_mode_manager_mode_allowed(11)) {
            return 11;
        }
        if (system_mode_manager_mode_allowed(10)) {
            return 10;
        }
    } else {
        uint8_t m = pick_preferred_mode_for_wan_type(SYSTEM_WAN_USB_MODEM);
        if (m != 0) {
            return m;
        }
    }
    return system_mode_manager_pick_hw_default_mode();
}

static const char *working_mode_tag_from_id(uint8_t id)
{
    const system_mode_profile_t *p = system_mode_manager_get_profile(id);
    if (!p) {
        return "4g";
    }
    if (p->wan_type == SYSTEM_WAN_NONE && p->lan_softap) {
        return "ap";
    }
    if (p->needs_sta && p->lan_softap) {
        return "router";
    }
    return "4g";
}

static void fmt_mac(char *out, size_t outsz, const uint8_t *m)
{
    snprintf(out, outsz, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static wifi_auth_mode_t enc_str_to_auth(const char *s)
{
    if (!s) {
        return WIFI_AUTH_WPA2_PSK;
    }
    if (strcmp(s, "OPEN") == 0) {
        return WIFI_AUTH_OPEN;
    }
    if (strcmp(s, "WPA-PSK") == 0) {
        return WIFI_AUTH_WPA_PSK;
    }
    if (strcmp(s, "WPA2-PSK") == 0) {
        return WIFI_AUTH_WPA2_PSK;
    }
    if (strcmp(s, "WPA3-SAE") == 0) {
#if defined(WIFI_AUTH_WPA3_SAE)
        return WIFI_AUTH_WPA3_SAE;
#else
        return WIFI_AUTH_WPA2_PSK;
#endif
    }
    return WIFI_AUTH_WPA2_PSK;
}

static const char *auth_to_enc_str(wifi_auth_mode_t a)
{
    switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
#if defined(WIFI_AUTH_WPA3_SAE)
    case WIFI_AUTH_WPA3_SAE: return "WPA3-SAE";
#endif
    default: return "WPA2-PSK";
    }
}

esp_err_t web_service_get_work_mode_u8(uint8_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    return load_work_mode_u8(out);
}

esp_err_t web_service_apply_work_mode_id(uint8_t mode_id)
{
    if (!system_mode_manager_get_profile(mode_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!system_mode_manager_mode_allowed(mode_id)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t ret = save_work_mode_u8(mode_id);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = system_mode_manager_apply(mode_id);
    if (ret != ESP_OK) {
        return ret;
    }
    nvs_handle_t hh = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &hh) == ESP_OK) {
        const char *tag = working_mode_tag_from_id(mode_id);
        nvs_set_str(hh, NVS_KEY_UIWM, tag);
        const system_mode_profile_t *pm = system_mode_manager_get_profile(mode_id);
        if (pm) {
            nvs_set_u8(hh, NVS_KEY_WANTYPE, (uint8_t)pm->wan_type);
        }
        nvs_commit(hh);
        nvs_close(hh);
    }
    return ESP_OK;
}

void web_softap_restore_from_nvs(void)
{
    char ap_json[768] = {0};
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = sizeof(ap_json);
    if (nvs_get_str(h, NVS_KEY_APUI, ap_json, &n) != ESP_OK) {
        nvs_close(h);
        return;
    }
    nvs_close(h);

    cJSON *root = cJSON_Parse(ap_json);
    if (!root) {
        return;
    }
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return;
    }

    wifi_config_t wcfg = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &wcfg) != ESP_OK) {
        memset(&wcfg, 0, sizeof(wcfg));
    }
    strlcpy((char *)wcfg.ap.ssid, ssid->valuestring, sizeof(wcfg.ap.ssid));
    cJSON *pwd = cJSON_GetObjectItem(root, "password");
    if (cJSON_IsString(pwd)) {
        strlcpy((char *)wcfg.ap.password, pwd->valuestring, sizeof(wcfg.ap.password));
    }
    cJSON *enc = cJSON_GetObjectItem(root, "encryption_mode");
    wcfg.ap.authmode = cJSON_IsString(enc) ? enc_str_to_auth(enc->valuestring) : WIFI_AUTH_WPA2_PSK;
    cJSON *hid = cJSON_GetObjectItem(root, "hidden_ssid");
    wcfg.ap.ssid_hidden = (cJSON_IsBool(hid) && cJSON_IsTrue(hid)) ? 1 : 0;
    cJSON *ch = cJSON_GetObjectItem(root, "channel");
    if (cJSON_IsString(ch) && strcmp(ch->valuestring, "auto") != 0) {
        wcfg.ap.channel = (uint8_t)atoi(ch->valuestring);
    }
    wcfg.ap.max_connection = 8;

    esp_err_t e = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "softap NVS restore: esp_wifi_set_config failed: %s", esp_err_to_name(e));
    } else {
        ESP_LOGI(TAG, "SoftAP config restored from NVS (ssid=%s)", wcfg.ap.ssid);
    }
    cJSON_Delete(root);
}

static esp_err_t uri_dashboard_overview_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }

    cJSON *sys = cJSON_CreateObject();
    uint8_t mode = 0;
    load_work_mode_u8(&mode);
    const system_mode_profile_t *prof = system_mode_manager_get_profile(mode);
    cJSON_AddStringToObject(sys, "system_mode", prof && prof->label ? prof->label : "—");
    cJSON_AddStringToObject(sys, "firmware_version", WEB_UI_FW_VERSION);
    cJSON_AddStringToObject(sys, "model", "4G_NIC");
    size_t total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t freeb = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    int mem_pct = (total > 0) ? (int)((total - freeb) * 100 / total) : 0;
    cJSON_AddNumberToObject(sys, "memory_percent", mem_pct);
    cJSON_AddNullToObject(sys, "cpu_percent");
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_info);
    cJSON_AddStringToObject(sys, "system_time", tbuf);
    cJSON_AddNumberToObject(sys, "uptime_s", (double)(esp_timer_get_time() / 1000000LL));
    wifi_sta_list_t wl = {0};
    int ou = 0;
    if (esp_wifi_ap_get_sta_list(&wl) == ESP_OK) {
        ou = wl.num;
    }
    cJSON_AddNumberToObject(sys, "online_users", ou);
    cJSON_AddItemToObject(root, "system", sys);

    cJSON *cell = cJSON_CreateObject();
    cJSON_AddStringToObject(cell, "operator", "--");
    cJSON_AddStringToObject(cell, "network_mode", "--");
    cJSON_AddStringToObject(cell, "imsi", "--");
    cJSON_AddStringToObject(cell, "imei", "--");
    cJSON_AddStringToObject(cell, "iccid", "--");
    cJSON_AddStringToObject(cell, "signal", "--");
    cJSON_AddNumberToObject(cell, "signal_rssi", 99);
    cJSON_AddNumberToObject(cell, "signal_ber", 99);
    cJSON_AddNumberToObject(cell, "network_act", -1);
    cJSON_AddStringToObject(cell, "manufacturer", "--");
    cJSON_AddStringToObject(cell, "module_name", "--");
    cJSON_AddStringToObject(cell, "fw_version", "--");
    cJSON_AddBoolToObject(cell, "ppp_has_ip", false);
    uint16_t vid = 0;
    uint16_t pid = 0;
    system_usb_cat1_detect_last_ids(&vid, &pid);
    char usbinfo[48];
    snprintf(usbinfo, sizeof(usbinfo), "vid:pid %04x:%04x", (unsigned)vid, (unsigned)pid);
    cJSON_AddStringToObject(cell, "usb_probe", usbinfo);
    cJSON_AddBoolToObject(cell, "usb_lte_ready", hw_usb_lte());
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM)
    esp_bridge_modem_info_t mi;
    if (esp_bridge_modem_get_info(&mi) == ESP_OK && mi.present) {
        cJSON_ReplaceItemInObject(cell, "operator", cJSON_CreateString(mi.operator_name));
        cJSON_ReplaceItemInObject(cell, "network_mode", cJSON_CreateString(mi.network_mode));
        cJSON_ReplaceItemInObject(cell, "imsi", cJSON_CreateString(mi.imsi));
        cJSON_ReplaceItemInObject(cell, "imei", cJSON_CreateString(mi.imei));
        cJSON_ReplaceItemInObject(cell, "iccid", cJSON_CreateString(mi.iccid));
        cJSON_ReplaceItemInObject(cell, "signal_rssi", cJSON_CreateNumber(mi.rssi));
        cJSON_ReplaceItemInObject(cell, "signal_ber", cJSON_CreateNumber(mi.ber));
        cJSON_ReplaceItemInObject(cell, "network_act", cJSON_CreateNumber(mi.act));
        cJSON_ReplaceItemInObject(cell, "manufacturer", cJSON_CreateString(mi.manufacturer));
        cJSON_ReplaceItemInObject(cell, "module_name", cJSON_CreateString(mi.module_name));
        cJSON_ReplaceItemInObject(cell, "fw_version", cJSON_CreateString(mi.fw_version));
        cJSON_ReplaceItemInObject(cell, "ppp_has_ip", cJSON_CreateBool(mi.ppp_has_ip));
        char sig[32];
        if (mi.rssi >= 0 && mi.rssi <= 31) {
            const int dbm = -113 + (2 * mi.rssi);
            snprintf(sig, sizeof(sig), "%d dBm", dbm);
        } else {
            snprintf(sig, sizeof(sig), "--");
        }
        cJSON_ReplaceItemInObject(cell, "signal", cJSON_CreateString(sig));
    }
#endif
    cJSON_AddItemToObject(root, "cellular", cell);

    cJSON *ifaces = cJSON_CreateArray();
    const char *ifplan[][2] = {
        {"wanrelay", "PPP_DEF"},
        {"lanrelay", "WIFI_AP_DEF"},
        {"lan_eth", "ETH_LAN"},
        {"sta", "WIFI_STA_DEF"},
        {"eth_wan", "ETH_WAN"},
    };
    for (size_t i = 0; i < sizeof(ifplan) / sizeof(ifplan[0]); i++) {
        cJSON *io = cJSON_CreateObject();
        cJSON_AddStringToObject(io, "name", ifplan[i][0]);
        esp_netif_t *n = esp_netif_get_handle_from_ifkey(ifplan[i][1]);
        cJSON_AddBoolToObject(io, "up", n != NULL);
        if (n) {
            esp_netif_ip_info_t ipi = {0};
            if (esp_netif_get_ip_info(n, &ipi) == ESP_OK) {
                char ib[20];
                esp_ip4addr_ntoa(&ipi.ip, ib, sizeof(ib));
                cJSON_AddStringToObject(io, "address", ib);
            } else {
                cJSON_AddStringToObject(io, "address", "--");
            }
            uint8_t mac[6] = {0};
            if (esp_netif_get_mac(n, mac) == ESP_OK) {
                char mb[24];
                fmt_mac(mb, sizeof(mb), mac);
                cJSON_AddStringToObject(io, "mac", mb);
            } else {
                cJSON_AddStringToObject(io, "mac", "--");
            }
        } else {
            cJSON_AddStringToObject(io, "address", "--");
            cJSON_AddStringToObject(io, "mac", "--");
        }
        cJSON_AddStringToObject(io, "rx", "--");
        cJSON_AddStringToObject(io, "tx", "--");
        cJSON_AddItemToArray(ifaces, io);
    }
    cJSON_AddItemToObject(root, "interfaces", ifaces);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_network_config_get(httpd_req_t *req)
{
    (void)req;
    lan_ui_cfg_t lan;
    load_lan_ui(&lan);
    uint8_t mode = 0;
    load_work_mode_u8(&mode);
    const system_mode_profile_t *active = system_mode_manager_get_profile(mode);
    uint8_t wan_type = active ? (uint8_t)active->wan_type : (uint8_t)SYSTEM_WAN_NONE;
    const char *tag = working_mode_tag_from_id(mode);
    nvs_handle_t h = 0;
    uint8_t w4g = 1;
    char wnm[32] = "auto";
    char uiwm[16] = "4g";
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_WAN4G, &w4g);
        size_t n = sizeof(wnm);
        if (nvs_get_str(h, NVS_KEY_WANNM, wnm, &n) != ESP_OK) {
            strlcpy(wnm, "auto", sizeof(wnm));
        }
        n = sizeof(uiwm);
        if (nvs_get_str(h, NVS_KEY_UIWM, uiwm, &n) != ESP_OK) {
            strlcpy(uiwm, tag, sizeof(uiwm));
        }
        nvs_close(h);
    } else {
        strlcpy(uiwm, tag, sizeof(uiwm));
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "working_mode", uiwm);
    cJSON_AddNumberToObject(root, "work_mode_id", mode);
    cJSON_AddNumberToObject(root, "wan_type", wan_type);
    cJSON *lanj = cJSON_CreateObject();
    cJSON_AddStringToObject(lanj, "ip", lan.ip);
    cJSON_AddStringToObject(lanj, "mask", lan.mask);
    cJSON_AddBoolToObject(lanj, "dhcp_enabled", lan.dhcp);
    cJSON_AddStringToObject(lanj, "dhcp_start", lan.dhcp_start);
    cJSON_AddStringToObject(lanj, "dhcp_end", lan.dhcp_end);
    cJSON_AddNumberToObject(lanj, "lease_hours", lan.lease_hours);
    cJSON_AddStringToObject(lanj, "dns1", lan.dns1);
    cJSON_AddStringToObject(lanj, "dns2", lan.dns2);
    cJSON_AddItemToObject(root, "lan", lanj);
    cJSON *wanj = cJSON_CreateObject();
    cJSON_AddBoolToObject(wanj, "lte_enabled", w4g != 0);
    cJSON_AddStringToObject(wanj, "network_mode", wnm);
    cJSON_AddItemToObject(root, "wan", wanj);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_network_config_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 2048);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *wmid = cJSON_GetObjectItem(root, "work_mode_id");
    cJSON *wtype = cJSON_GetObjectItem(root, "wan_type");
    uint8_t m = 0;
    bool mode_selected = false;
    if (cJSON_IsNumber(wmid)) {
        m = (uint8_t)wmid->valuedouble;
        mode_selected = true;
    } else if (cJSON_IsNumber(wtype)) {
        system_wan_type_t wt = (system_wan_type_t)((uint8_t)wtype->valuedouble);
        m = pick_preferred_mode_for_wan_type(wt);
        if (m == 0) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"wan_type not available for this hardware\"}");
        }
        mode_selected = true;
    }
    if (mode_selected) {
        if (!system_mode_manager_get_profile(m)) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"unknown work_mode_id\"}");
        }
        if (!system_mode_manager_mode_allowed(m)) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"work_mode not allowed for this hardware\"}");
        }
        esp_err_t mer = save_work_mode_u8(m);
        if (mer != ESP_OK) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs save work_mode failed\"}");
        }
        (void)system_mode_manager_apply(m);
        nvs_handle_t hh = 0;
        if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &hh) == ESP_OK) {
            const char *tag = working_mode_tag_from_id(m);
            nvs_set_str(hh, NVS_KEY_UIWM, tag);
            const system_mode_profile_t *pm = system_mode_manager_get_profile(m);
            if (pm) {
                nvs_set_u8(hh, NVS_KEY_WANTYPE, (uint8_t)pm->wan_type);
            }
            nvs_commit(hh);
            nvs_close(hh);
        }
    } else {
        cJSON *wm = cJSON_GetObjectItem(root, "working_mode");
        if (cJSON_IsString(wm)) {
            nvs_handle_t hh = 0;
            if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &hh) == ESP_OK) {
                nvs_set_str(hh, NVS_KEY_UIWM, wm->valuestring);
                nvs_commit(hh);
                nvs_close(hh);
            }
            uint8_t mid = pick_mode_for_ui_working_mode(wm->valuestring);
            save_work_mode_u8(mid);
            system_mode_manager_apply(mid);
            nvs_handle_t wh = 0;
            if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &wh) == ESP_OK) {
                const system_mode_profile_t *pm = system_mode_manager_get_profile(mid);
                if (pm) {
                    nvs_set_u8(wh, NVS_KEY_WANTYPE, (uint8_t)pm->wan_type);
                    nvs_commit(wh);
                }
                nvs_close(wh);
            }
        }
    }
    cJSON *lanj = cJSON_GetObjectItem(root, "lan");
    if (cJSON_IsObject(lanj)) {
        lan_ui_cfg_t lan;
        load_lan_ui(&lan);
        cJSON *x = cJSON_GetObjectItem(lanj, "ip");
        if (cJSON_IsString(x)) {
            strlcpy(lan.ip, x->valuestring, sizeof(lan.ip));
        }
        x = cJSON_GetObjectItem(lanj, "mask");
        if (cJSON_IsString(x)) {
            strlcpy(lan.mask, x->valuestring, sizeof(lan.mask));
        }
        x = cJSON_GetObjectItem(lanj, "dhcp_enabled");
        if (cJSON_IsBool(x)) {
            lan.dhcp = cJSON_IsTrue(x);
        }
        x = cJSON_GetObjectItem(lanj, "dhcp_start");
        if (cJSON_IsString(x)) {
            strlcpy(lan.dhcp_start, x->valuestring, sizeof(lan.dhcp_start));
        }
        x = cJSON_GetObjectItem(lanj, "dhcp_end");
        if (cJSON_IsString(x)) {
            strlcpy(lan.dhcp_end, x->valuestring, sizeof(lan.dhcp_end));
        }
        x = cJSON_GetObjectItem(lanj, "lease_hours");
        if (cJSON_IsNumber(x)) {
            lan.lease_hours = (int)x->valuedouble;
        }
        x = cJSON_GetObjectItem(lanj, "dns1");
        if (cJSON_IsString(x)) {
            strlcpy(lan.dns1, x->valuestring, sizeof(lan.dns1));
        }
        x = cJSON_GetObjectItem(lanj, "dns2");
        if (cJSON_IsString(x)) {
            strlcpy(lan.dns2, x->valuestring, sizeof(lan.dns2));
        }
        if (!ip_text_valid(lan.ip) || !ip_text_valid(lan.mask) || !ip_text_valid(lan.dns1) || !ip_text_valid(lan.dns2)) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid ipv4\"}");
        }
        save_lan_ui(&lan);
    }
    cJSON *wanj = cJSON_GetObjectItem(root, "wan");
    if (cJSON_IsObject(wanj)) {
        nvs_handle_t hh = 0;
        if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &hh) == ESP_OK) {
            cJSON *e = cJSON_GetObjectItem(wanj, "lte_enabled");
            if (cJSON_IsBool(e)) {
                nvs_set_u8(hh, NVS_KEY_WAN4G, cJSON_IsTrue(e) ? 1u : 0u);
            }
            cJSON *nm = cJSON_GetObjectItem(wanj, "network_mode");
            if (cJSON_IsString(nm)) {
                nvs_set_str(hh, NVS_KEY_WANNM, nm->valuestring);
            }
            nvs_commit(hh);
            nvs_close(hh);
        }
    }
    cJSON_Delete(root);
    log_ring_fmt("[NET] network config saved");
    return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"LAN 参数已写入 NVS；部分项需重启后由系统统一应用。\"}");
}

static esp_err_t uri_network_apn_get(httpd_req_t *req)
{
    (void)req;
    char apn[48] = {0};
    char user[48] = {0};
    char pwd[48] = {0};
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(apn);
        nvs_get_str(h, NVS_KEY_APN, apn, &n);
        n = sizeof(user);
        nvs_get_str(h, NVS_KEY_APNUSER, user, &n);
        n = sizeof(pwd);
        nvs_get_str(h, NVS_KEY_APNPWD, pwd, &n);
        nvs_close(h);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "apn", apn);
    cJSON_AddStringToObject(root, "username", user);
    cJSON_AddStringToObject(root, "password", pwd);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_network_apn_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 512);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    nvs_handle_t h = 0;
    esp_err_t er = nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &h);
    if (er != ESP_OK) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs open failed\"}");
    }
    cJSON *a = cJSON_GetObjectItem(root, "apn");
    if (cJSON_IsString(a)) {
        nvs_set_str(h, NVS_KEY_APN, a->valuestring);
    }
    a = cJSON_GetObjectItem(root, "username");
    if (cJSON_IsString(a)) {
        nvs_set_str(h, NVS_KEY_APNUSER, a->valuestring);
    }
    a = cJSON_GetObjectItem(root, "password");
    if (cJSON_IsString(a)) {
        nvs_set_str(h, NVS_KEY_APNPWD, a->valuestring);
    }
    nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(root);
    log_ring_fmt("[APN] apn saved to NVS (runtime dialer may需单独集成)");
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_wifi_ap_get(httpd_req_t *req)
{
    (void)req;
    wifi_config_t wcfg = {0};
    esp_wifi_get_config(WIFI_IF_AP, &wcfg);
    char ap_json[512] = {0};
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(ap_json);
        nvs_get_str(h, NVS_KEY_APUI, ap_json, &n);
        nvs_close(h);
    }
    cJSON *extra = ap_json[0] ? cJSON_Parse(ap_json) : NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "wifi_enabled", true);
    cJSON_AddStringToObject(root, "ssid", (char *)wcfg.ap.ssid);
    cJSON_AddStringToObject(root, "encryption_mode", auth_to_enc_str(wcfg.ap.authmode));
    cJSON_AddStringToObject(root, "password", "");
    cJSON_AddBoolToObject(root, "hidden_ssid", wcfg.ap.ssid_hidden != 0);
    cJSON_AddStringToObject(root, "protocol", "auto");
    cJSON_AddStringToObject(root, "bandwidth", "auto");
    cJSON_AddStringToObject(root, "channel", "auto");
    cJSON_AddStringToObject(root, "signal_strength", "auto");
    cJSON_AddBoolToObject(root, "wps_enabled", true);
    cJSON_AddBoolToObject(root, "wps_pin_enabled", false);
    if (extra) {
        cJSON *it = NULL;
        it = cJSON_GetObjectItem(extra, "protocol");
        if (cJSON_IsString(it)) {
            cJSON_DeleteItemFromObject(root, "protocol");
            cJSON_AddStringToObject(root, "protocol", it->valuestring);
        }
        it = cJSON_GetObjectItem(extra, "bandwidth");
        if (cJSON_IsString(it)) {
            cJSON_DeleteItemFromObject(root, "bandwidth");
            cJSON_AddStringToObject(root, "bandwidth", it->valuestring);
        }
        it = cJSON_GetObjectItem(extra, "channel");
        if (cJSON_IsString(it)) {
            cJSON_DeleteItemFromObject(root, "channel");
            cJSON_AddStringToObject(root, "channel", it->valuestring);
        }
        it = cJSON_GetObjectItem(extra, "signal_strength");
        if (cJSON_IsString(it)) {
            cJSON_DeleteItemFromObject(root, "signal_strength");
            cJSON_AddStringToObject(root, "signal_strength", it->valuestring);
        }
        it = cJSON_GetObjectItem(extra, "wps_enabled");
        if (cJSON_IsBool(it)) {
            cJSON_DeleteItemFromObject(root, "wps_enabled");
            cJSON_AddBoolToObject(root, "wps_enabled", cJSON_IsTrue(it));
        }
        it = cJSON_GetObjectItem(extra, "wps_pin_enabled");
        if (cJSON_IsBool(it)) {
            cJSON_DeleteItemFromObject(root, "wps_pin_enabled");
            cJSON_AddBoolToObject(root, "wps_pin_enabled", cJSON_IsTrue(it));
        }
        cJSON_Delete(extra);
    }
    if (wcfg.ap.channel) {
        char chs[8];
        snprintf(chs, sizeof(chs), "%d", wcfg.ap.channel);
        cJSON_DeleteItemFromObject(root, "channel");
        cJSON_AddStringToObject(root, "channel", chs);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_wifi_ap_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 1024);
    if (!body) {
        ESP_LOGE(TAG, "wifi_ap_post: failed to receive body");
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    ESP_LOGI(TAG, "wifi_ap_post: received body[%d]", (int)req->content_len);
    /* Print hex dump for debugging */
    if (req->content_len > 0) {
        int print_len = req->content_len < 128 ? req->content_len : 128;
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, body, print_len, ESP_LOG_INFO);
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        const char *err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "wifi_ap_post: JSON parse failed at: %s", err_ptr ? err_ptr : "unknown");
        ESP_LOGE(TAG, "wifi_ap_post: raw body len=%d", (int)req->content_len);
        free(body);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    free(body);
    wifi_config_t wcfg = {0};
    esp_wifi_get_config(WIFI_IF_AP, &wcfg);

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd = cJSON_GetObjectItem(root, "password");
    cJSON *enc = cJSON_GetObjectItem(root, "encryption_mode");
    cJSON *hid = cJSON_GetObjectItem(root, "hidden_ssid");
    if (cJSON_IsString(ssid)) {
        strlcpy((char *)wcfg.ap.ssid, ssid->valuestring, sizeof(wcfg.ap.ssid));
    }
    if (cJSON_IsString(pwd) && strlen(pwd->valuestring) > 0) {
        strlcpy((char *)wcfg.ap.password, pwd->valuestring, sizeof(wcfg.ap.password));
    }
    wcfg.ap.authmode = cJSON_IsString(enc) ? enc_str_to_auth(enc->valuestring) : WIFI_AUTH_WPA2_PSK;
    wcfg.ap.ssid_hidden = (cJSON_IsBool(hid) && cJSON_IsTrue(hid)) ? 1 : 0;
    cJSON *ch = cJSON_GetObjectItem(root, "channel");
    if (cJSON_IsString(ch) && strcmp(ch->valuestring, "auto") != 0) {
        wcfg.ap.channel = (uint8_t)atoi(ch->valuestring);
    }
    wcfg.ap.max_connection = 8;

    esp_err_t werr = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    if (werr != ESP_OK) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"wifi ap set failed\"}");
    }

    /* Persist full SoftAP fields for boot restore (WIFI_STORAGE_RAM loses RAM config). */
    cJSON *extra = cJSON_CreateObject();
    cJSON_AddStringToObject(extra, "ssid", (char *)wcfg.ap.ssid);
    cJSON_AddStringToObject(extra, "password", (char *)wcfg.ap.password);
    cJSON_AddStringToObject(extra, "encryption_mode", auth_to_enc_str(wcfg.ap.authmode));
    cJSON_AddBoolToObject(extra, "hidden_ssid", wcfg.ap.ssid_hidden != 0);
    if (wcfg.ap.channel) {
        char chs[8];
        snprintf(chs, sizeof(chs), "%d", wcfg.ap.channel);
        cJSON_AddStringToObject(extra, "channel", chs);
    } else {
        cJSON_AddStringToObject(extra, "channel", "auto");
    }
    cJSON *p = cJSON_GetObjectItem(root, "protocol");
    if (cJSON_IsString(p)) {
        cJSON_AddStringToObject(extra, "protocol", p->valuestring);
    }
    p = cJSON_GetObjectItem(root, "bandwidth");
    if (cJSON_IsString(p)) {
        cJSON_AddStringToObject(extra, "bandwidth", p->valuestring);
    }
    p = cJSON_GetObjectItem(root, "signal_strength");
    if (cJSON_IsString(p)) {
        cJSON_AddStringToObject(extra, "signal_strength", p->valuestring);
    }
    p = cJSON_GetObjectItem(root, "wps_enabled");
    if (cJSON_IsBool(p)) {
        cJSON_AddBoolToObject(extra, "wps_enabled", cJSON_IsTrue(p));
    }
    p = cJSON_GetObjectItem(root, "wps_pin_enabled");
    if (cJSON_IsBool(p)) {
        cJSON_AddBoolToObject(extra, "wps_pin_enabled", cJSON_IsTrue(p));
    }
    char *exs = cJSON_PrintUnformatted(extra);
    cJSON_Delete(extra);
    if (exs) {
        nvs_handle_t nh = 0;
        if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, NVS_KEY_APUI, exs);
            nvs_commit(nh);
            nvs_close(nh);
        }
        free(exs);
    }
    cJSON_Delete(root);
    log_ring_fmt("[WIFI] SoftAP config updated");
    return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"SoftAP 参数已写入；部分终端可能需重连。\"}");
}

static esp_err_t uri_system_probes_get(httpd_req_t *req)
{
    (void)req;
    char p1[20] = "114.114.114.114";
    char p2[20] = "8.8.8.8";
    char p3[20] = "208.67.222.222";
    char p4[20] = "223.5.5.5";
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(p1);
        nvs_get_str(h, NVS_KEY_PROBE1, p1, &n);
        n = sizeof(p2);
        nvs_get_str(h, NVS_KEY_PROBE2, p2, &n);
        n = sizeof(p3);
        nvs_get_str(h, NVS_KEY_PROBE3, p3, &n);
        n = sizeof(p4);
        nvs_get_str(h, NVS_KEY_PROBE4, p4, &n);
        nvs_close(h);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "detection_ip1", p1);
    cJSON_AddStringToObject(root, "detection_ip2", p2);
    cJSON_AddStringToObject(root, "detection_ip3", p3);
    cJSON_AddStringToObject(root, "detection_ip4", p4);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_system_probes_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 512);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    const char *keys[] = {"detection_ip1", "detection_ip2", "detection_ip3", "detection_ip4"};
    const char *nvskeys[] = {NVS_KEY_PROBE1, NVS_KEY_PROBE2, NVS_KEY_PROBE3, NVS_KEY_PROBE4};
    for (int i = 0; i < 4; i++) {
        cJSON *it = cJSON_GetObjectItem(root, keys[i]);
        if (cJSON_IsString(it) && !ip_text_valid(it->valuestring)) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid ipv4\"}");
        }
    }
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &h) != ESP_OK) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs open failed\"}");
    }
    for (int i = 0; i < 4; i++) {
        cJSON *it = cJSON_GetObjectItem(root, keys[i]);
        if (cJSON_IsString(it)) {
            nvs_set_str(h, nvskeys[i], it->valuestring);
        }
    }
    nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(root);
    log_ring_fmt("[PROBE] detection IPs saved");
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_system_time_get(httpd_req_t *req)
{
    (void)req;
    apply_timezone_from_nvs();
    char tz[64] = "(GMT+08:00) Asia/Shanghai";
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(tz);
        if (nvs_get_str(h, NVS_KEY_TZ, tz, &n) != ESP_OK) {
            strlcpy(tz, "(GMT+08:00) Asia/Shanghai", sizeof(tz));
        }
        nvs_close(h);
    }
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_info);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "system_time", tbuf);
    cJSON_AddStringToObject(root, "firmware_version", WEB_UI_FW_VERSION);
    cJSON_AddStringToObject(root, "timezone", tz);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_system_time_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 512);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *tz = cJSON_GetObjectItem(root, "timezone");
    if (cJSON_IsString(tz)) {
        nvs_handle_t h = 0;
        if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, NVS_KEY_TZ, tz->valuestring);
            nvs_commit(h);
            nvs_close(h);
        }
        setenv("TZ", tz_display_to_posix(tz->valuestring), 1);
        tzset();
    }
    cJSON_Delete(root);
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_system_sync_time_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 256);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *ts = cJSON_GetObjectItem(root, "local_timestamp_ms");
    if (!cJSON_IsNumber(ts)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"local_timestamp_ms required\"}");
    }
    struct timeval tv = {0};
    double ms = ts->valuedouble;
    tv.tv_sec = (time_t)(ms / 1000.0);
    tv.tv_usec = (suseconds_t)((long long)ms % 1000LL) * 1000;
    
    /* Debug: log the received timestamp and converted time */
    time_t recv_time = tv.tv_sec;
    struct tm tm_info;
    localtime_r(&recv_time, &tm_info);
    ESP_LOGI(TAG, "[TIME] Received timestamp: %.0f ms", ms);
    ESP_LOGI(TAG, "[TIME] Converted to: %04d-%02d-%02d %02d:%02d:%02d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    
    settimeofday(&tv, NULL);
    cJSON_Delete(root);
    log_ring_fmt("[TIME] time synced from browser");
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_system_reboot_post(httpd_req_t *req)
{
    (void)req;
    log_ring_fmt("[SYS] reboot requested via web");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static esp_err_t uri_system_reboot_schedule_get(httpd_req_t *req)
{
    (void)req;
    uint8_t en = 0;
    int32_t hh = 3;
    int32_t mm = 30;
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_SCH_EN, &en);
        nvs_get_i32(h, NVS_KEY_SCH_H, &hh);
        nvs_get_i32(h, NVS_KEY_SCH_M, &mm);
        nvs_close(h);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", en != 0);
    cJSON_AddNumberToObject(root, "hour", hh);
    cJSON_AddNumberToObject(root, "minute", mm);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_system_reboot_schedule_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 256);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &h) != ESP_OK) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs open failed\"}");
    }
    cJSON *it = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(it)) {
        nvs_set_u8(h, NVS_KEY_SCH_EN, cJSON_IsTrue(it) ? 1u : 0u);
    }
    it = cJSON_GetObjectItem(root, "hour");
    if (cJSON_IsNumber(it)) {
        nvs_set_i32(h, NVS_KEY_SCH_H, (int32_t)it->valuedouble);
    }
    it = cJSON_GetObjectItem(root, "minute");
    if (cJSON_IsNumber(it)) {
        nvs_set_i32(h, NVS_KEY_SCH_M, (int32_t)it->valuedouble);
    }
    nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(root);
    return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"定时重启配置已保存（固件侧调度待集成时可仅作占位）。\"}");
}

static esp_err_t uri_system_logs_get(httpd_req_t *req)
{
    (void)req;
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    int n = s_log_ring_count;
    int start = (s_log_ring_pos - n + LOG_RING_LINES) % LOG_RING_LINES;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % LOG_RING_LINES;
        cJSON_AddItemToArray(arr, cJSON_CreateString(s_log_ring[idx]));
    }
    cJSON_AddItemToObject(root, "logs", arr);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_system_logs_delete(httpd_req_t *req)
{
    (void)req;
    s_log_ring_pos = 0;
    s_log_ring_count = 0;
    memset(s_log_ring, 0, sizeof(s_log_ring));
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_system_login_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 256);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *u = cJSON_GetObjectItem(root, "username");
    cJSON *p = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(u) || !cJSON_IsString(p)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"username/password required\"}");
    }

    char admin_pwd[68] = {0};
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(admin_pwd);
        nvs_get_str(h, NVS_KEY_ADMIN, admin_pwd, &n);
        nvs_close(h);
    }
    if (admin_pwd[0] == '\0') {
        strlcpy(admin_pwd, "admin", sizeof(admin_pwd));
    }

    const bool ok = (strcmp(u->valuestring, "admin") == 0) && (strcmp(p->valuestring, admin_pwd) == 0);
    cJSON_Delete(root);
    if (!ok) {
        return send_json(req, 401, "{\"status\":\"error\",\"message\":\"用户名或密码错误\"}");
    }
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_system_password_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 384);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *oldp = cJSON_GetObjectItem(root, "old_password");
    cJSON *newp = cJSON_GetObjectItem(root, "new_password");
    if (!cJSON_IsString(newp)) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"new_password required\"}");
    }
    char cur[68] = {0};
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(cur);
        nvs_get_str(h, NVS_KEY_ADMIN, cur, &n);
        nvs_close(h);
    }
    if (cur[0] != '\0') {
        if (!cJSON_IsString(oldp) || strcmp(oldp->valuestring, cur) != 0) {
            cJSON_Delete(root);
            return send_json(req, 400, "{\"status\":\"error\",\"message\":\"原密码不正确\"}");
        }
    }
    if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &h) != ESP_OK) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs open failed\"}");
    }
    nvs_set_str(h, NVS_KEY_ADMIN, newp->valuestring);
    nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(root);
    log_ring_fmt("[AUTH] admin password changed");
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t nvs_erase_ns(const char *name)
{
    nvs_handle_t h = 0;
    esp_err_t e = nvs_open(name, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_erase_all(h);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

static esp_err_t uri_system_factory_reset_post(httpd_req_t *req)
{
    (void)req;
    log_ring_fmt("[SYS] factory reset requested");
    nvs_erase_ns(NVS_NS_WEBUI);
    nvs_erase_ns(NVS_NS_LANUI);
    nvs_erase_ns("wifi_cfg");
    nvs_erase_ns(NVS_NS_ETH_WAN);
    nvs_erase_ns(NVS_NS_UI);
    return send_json(req, 200, "{\"status\":\"success\",\"message\":\"NVS 已清除，请重启设备\",\"reboot_required\":true}");
}

static esp_err_t uri_system_config_export_get(httpd_req_t *req)
{
    (void)req;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "export_version", "1");
    lan_ui_cfg_t lan;
    load_lan_ui(&lan);
    cJSON *lanj = cJSON_CreateObject();
    cJSON_AddStringToObject(lanj, "ip", lan.ip);
    cJSON_AddStringToObject(lanj, "mask", lan.mask);
    cJSON_AddBoolToObject(lanj, "dhcp", lan.dhcp);
    cJSON_AddItemToObject(root, "lan_ui", lanj);

    uint8_t mode = 0;
    load_work_mode_u8(&mode);
    cJSON_AddNumberToObject(root, "work_mode", mode);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_system_config_import_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 4096);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *wm = cJSON_GetObjectItem(root, "work_mode");
    if (cJSON_IsNumber(wm)) {
        save_work_mode_u8((uint8_t)wm->valuedouble);
    }
    cJSON_Delete(root);
    log_ring_fmt("[SYS] config import (partial) applied");
    return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"已导入部分字段；完整恢复建议配合出厂缺省。\"}");
}

static esp_err_t uri_system_firmware_post(httpd_req_t *req)
{
    (void)req;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "501 Not Implemented");
    return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"本地固件刷写未在此构建中实现\"}");
}

static esp_err_t uri_system_upgrade_status_get(httpd_req_t *req)
{
    (void)req;
    return send_json(req, 200, "{\"state\":\"idle\",\"progress\":0}");
}

static esp_err_t uri_users_online_get(httpd_req_t *req)
{
    (void)req;
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    wifi_sta_list_t wl = {0};
    int total = 0;
    if (esp_wifi_ap_get_sta_list(&wl) == ESP_OK) {
        total = wl.num;
        for (int i = 0; i < wl.num; i++) {
            cJSON *o = cJSON_CreateObject();
            char mac[20];
            fmt_mac(mac, sizeof(mac), wl.sta[i].mac);
            cJSON_AddNumberToObject(o, "id", i + 1);
            cJSON_AddStringToObject(o, "hostname", "client");
            cJSON_AddStringToObject(o, "online_duration", "—");
            cJSON_AddStringToObject(o, "ip_address", "—");
            cJSON_AddStringToObject(o, "mac_address", mac);
            cJSON_AddBoolToObject(o, "ip_mac_bind", false);
            cJSON_AddBoolToObject(o, "allow_access", true);
            cJSON_AddItemToArray(arr, o);
        }
    }
    cJSON_AddItemToObject(root, "users", arr);
    cJSON_AddNumberToObject(root, "total", total);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_users_blacklist_get(httpd_req_t *req)
{
    (void)req;
    return send_json(req, 200, "{\"items\":[]}");
}

static esp_err_t uri_traffic_get(httpd_req_t *req)
{
    (void)req;
    uint8_t en = 1;
    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS_WEBUI, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_TRAFFIC, &en);
        nvs_close(h);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "traffic_enabled", en != 0);
    cJSON_AddStringToObject(root, "note", "统计功能依赖后续驱动/计数器集成");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"oom\"}");
    }
    esp_err_t e = send_json(req, 200, out);
    free(out);
    return e;
}

static esp_err_t uri_traffic_post(httpd_req_t *req)
{
    char *body = http_recv_body(req, 128);
    if (!body) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid body\"}");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }
    cJSON *en = cJSON_GetObjectItem(root, "traffic_enabled");
    if (cJSON_IsBool(en)) {
        nvs_handle_t h = 0;
        if (nvs_open(NVS_NS_WEBUI, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, NVS_KEY_TRAFFIC, cJSON_IsTrue(en) ? 1u : 0u);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    cJSON_Delete(root);
    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t register_handlers(httpd_handle_t server)
{
    httpd_uri_t wifi_get = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = uri_wifi_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_get), TAG, "register wifi get failed");

    httpd_uri_t wifi_post = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = uri_wifi_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_post), TAG, "register wifi post failed");

    httpd_uri_t wifi_clear_post = {
        .uri = "/api/wifi/clear",
        .method = HTTP_POST,
        .handler = uri_wifi_clear_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_clear_post), TAG, "register wifi clear post failed");

    httpd_uri_t wifi_scan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = uri_wifi_scan_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_scan), TAG, "register wifi scan failed");

    httpd_uri_t eth_wan_get = {
        .uri = "/api/eth_wan",
        .method = HTTP_GET,
        .handler = uri_eth_wan_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &eth_wan_get), TAG, "register eth wan get failed");

    httpd_uri_t eth_wan_post = {
        .uri = "/api/eth_wan",
        .method = HTTP_POST,
        .handler = uri_eth_wan_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &eth_wan_post), TAG, "register eth wan post failed");

    httpd_uri_t eth_wan_clear_post = {
        .uri = "/api/eth_wan/clear",
        .method = HTTP_POST,
        .handler = uri_eth_wan_clear_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &eth_wan_clear_post), TAG, "register eth wan clear failed");

    httpd_uri_t sys_hw = {
        .uri = "/api/system/hw",
        .method = HTTP_GET,
        .handler = uri_system_hw_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &sys_hw), TAG, "register system hw failed");

    httpd_uri_t sys_over = {
        .uri = "/api/system/overview",
        .method = HTTP_GET,
        .handler = uri_system_overview_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &sys_over), TAG, "register overview failed");

    httpd_uri_t mode_get = {
        .uri = "/api/mode",
        .method = HTTP_GET,
        .handler = uri_mode_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &mode_get), TAG, "register mode get failed");

    httpd_uri_t mode_post = {
        .uri = "/api/mode",
        .method = HTTP_POST,
        .handler = uri_mode_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &mode_post), TAG, "register mode post failed");

    httpd_uri_t mode_apply = {
        .uri = "/api/mode/apply",
        .method = HTTP_POST,
        .handler = uri_mode_apply_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &mode_apply), TAG, "register mode apply failed");

    httpd_uri_t mode_status = {
        .uri = "/api/mode/status",
        .method = HTTP_GET,
        .handler = uri_mode_status_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &mode_status), TAG, "register mode status failed");

    httpd_uri_t dash_over = {.uri = "/api/dashboard/overview", .method = HTTP_GET, .handler = uri_dashboard_overview_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &dash_over), TAG, "register dashboard failed");

    httpd_uri_t net_cfg_get = {.uri = "/api/network/config", .method = HTTP_GET, .handler = uri_network_config_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &net_cfg_get), TAG, "register net cfg get failed");

    httpd_uri_t net_cfg_post = {.uri = "/api/network/config", .method = HTTP_POST, .handler = uri_network_config_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &net_cfg_post), TAG, "register net cfg post failed");

    httpd_uri_t apn_get = {.uri = "/api/network/apn", .method = HTTP_GET, .handler = uri_network_apn_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &apn_get), TAG, "register apn get failed");

    httpd_uri_t apn_post = {.uri = "/api/network/apn", .method = HTTP_POST, .handler = uri_network_apn_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &apn_post), TAG, "register apn post failed");

    httpd_uri_t wifi_ap_g = {.uri = "/api/wifi/ap", .method = HTTP_GET, .handler = uri_wifi_ap_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_ap_g), TAG, "register wifi ap get failed");

    httpd_uri_t wifi_ap_p = {.uri = "/api/wifi/ap", .method = HTTP_POST, .handler = uri_wifi_ap_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_ap_p), TAG, "register wifi ap post failed");

    httpd_uri_t probe_g = {.uri = "/api/system/probes", .method = HTTP_GET, .handler = uri_system_probes_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &probe_g), TAG, "register probes get failed");

    httpd_uri_t probe_p = {.uri = "/api/system/probes", .method = HTTP_POST, .handler = uri_system_probes_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &probe_p), TAG, "register probes post failed");

    httpd_uri_t time_g = {.uri = "/api/system/time", .method = HTTP_GET, .handler = uri_system_time_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &time_g), TAG, "register time get failed");

    httpd_uri_t time_p = {.uri = "/api/system/time", .method = HTTP_POST, .handler = uri_system_time_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &time_p), TAG, "register time post failed");

    httpd_uri_t sync_t = {.uri = "/api/system/sync_time", .method = HTTP_POST, .handler = uri_system_sync_time_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &sync_t), TAG, "register sync time failed");

    httpd_uri_t reb = {.uri = "/api/system/reboot", .method = HTTP_POST, .handler = uri_system_reboot_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &reb), TAG, "register reboot failed");

    httpd_uri_t rbs_g = {.uri = "/api/system/reboot/schedule", .method = HTTP_GET, .handler = uri_system_reboot_schedule_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &rbs_g), TAG, "register reboot sch get failed");

    httpd_uri_t rbs_p = {.uri = "/api/system/reboot/schedule", .method = HTTP_POST, .handler = uri_system_reboot_schedule_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &rbs_p), TAG, "register reboot sch post failed");

    httpd_uri_t logs_g = {.uri = "/api/system/logs", .method = HTTP_GET, .handler = uri_system_logs_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &logs_g), TAG, "register logs get failed");

    httpd_uri_t logs_d = {.uri = "/api/system/logs", .method = HTTP_DELETE, .handler = uri_system_logs_delete};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &logs_d), TAG, "register logs del failed");

    httpd_uri_t login_p = {.uri = "/api/system/login", .method = HTTP_POST, .handler = uri_system_login_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &login_p), TAG, "register login failed");

    httpd_uri_t pwd_p = {.uri = "/api/system/password", .method = HTTP_POST, .handler = uri_system_password_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &pwd_p), TAG, "register pwd failed");

    httpd_uri_t fact = {.uri = "/api/system/factory_reset", .method = HTTP_POST, .handler = uri_system_factory_reset_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &fact), TAG, "register factory failed");

    httpd_uri_t cfg_ex = {.uri = "/api/system/config/export", .method = HTTP_GET, .handler = uri_system_config_export_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &cfg_ex), TAG, "register cfg export failed");

    httpd_uri_t cfg_im = {.uri = "/api/system/config/import", .method = HTTP_POST, .handler = uri_system_config_import_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &cfg_im), TAG, "register cfg import failed");

    httpd_uri_t fw_p = {.uri = "/api/system/firmware", .method = HTTP_POST, .handler = uri_system_firmware_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &fw_p), TAG, "register firmware failed");

    httpd_uri_t up_st = {.uri = "/api/system/upgrade_status", .method = HTTP_GET, .handler = uri_system_upgrade_status_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &up_st), TAG, "register upgrade status failed");

    httpd_uri_t u_on = {.uri = "/api/users/online", .method = HTTP_GET, .handler = uri_users_online_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &u_on), TAG, "register users online failed");

    httpd_uri_t u_bl = {.uri = "/api/users/blacklist", .method = HTTP_GET, .handler = uri_users_blacklist_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &u_bl), TAG, "register blacklist failed");

    httpd_uri_t tr_g = {.uri = "/api/traffic", .method = HTTP_GET, .handler = uri_traffic_get};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &tr_g), TAG, "register traffic get failed");

    httpd_uri_t tr_p = {.uri = "/api/traffic", .method = HTTP_POST, .handler = uri_traffic_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &tr_p), TAG, "register traffic post failed");

    httpd_uri_t static_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = uri_static_get,
    };
    return httpd_register_uri_handler(server, &static_get);
}

esp_err_t web_service_start(void)
{
    if (s_running) return ESP_OK;

    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    /* With SPIRAM_USE_MEMMAP, WiFi/lwIP stay in internal DRAM; 12KB httpd stack + server structs need headroom. */
    if (internal_free < 28 * 1024) {
        ESP_LOGW(TAG, "skip web UI: internal_free=%u (<28KiB; raise IDF+SPIRAM_MALLOC or trim WiFi/LwIP)", (unsigned)internal_free);
        return ESP_ERR_NO_MEM;
    }

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/www",
        .partition_label = "www",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "mount /www failed");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 56;
    if (internal_free >= 110 * 1024) {
        config.stack_size = 12288;
    } else if (internal_free >= 75 * 1024) {
        config.stack_size = 8192;
    } else if (internal_free >= 48 * 1024) {
        config.stack_size = 6144;
    } else {
        config.stack_size = 4096;
    }
    config.task_priority = 5;
    ESP_LOGI(TAG, "httpd internal_free=%u stack_size=%u", (unsigned)internal_free, (unsigned)config.stack_size);

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd start failed");
    ESP_RETURN_ON_ERROR(register_handlers(s_server), TAG, "register handlers failed");

    /* STA startup is owned by mode manager.
     * Do not re-apply saved STA here, or it races with system_mode_manager_apply()
     * during boot (can trigger WIFI_STATE errors and unstable STA netif state). */

    log_ring_append("[BOOT] web service ready");
    ESP_LOGI(TAG, "web service started");
    s_running = true;
    return ESP_OK;
}

esp_err_t web_service_stop(void)
{
    if (!s_running || s_server == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = httpd_stop(s_server);
    if (ret == ESP_OK) {
        s_server = NULL;
        s_running = false;
    }
    return ret;
}

bool web_service_is_running(void)
{
    return s_running;
}
