#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "system_w5500_detect.h"
#include "system_usb_cat1_detect.h"
#include "web_service.h"

static const char *TAG = "web_service";
static httpd_handle_t s_server = NULL;
static bool s_running = false;

#define NVS_NS_UI      "bridge_ui"
#define NVS_KEY_MODE   "work_mode"

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

/** Boot-time USB enumeration matched a known Cat1/modem (independent of menuconfig WAN role). */
static bool hw_usb_modem_probe(void)
{
#if CONFIG_SYSTEM_USB_CAT1_DETECT
    return system_usb_cat1_detect_present();
#else
    return false;
#endif
}

/** USB modem usable as external WAN in this build (modem netif enabled in menuconfig + probe hit). */
static bool hw_usb_lte(void)
{
#if CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM
    return system_usb_cat1_detect_present();
#else
    return false;
#endif
}

static bool hw_w5500(void)
{
    return system_w5500_detect_present();
}

static bool cap_softap(void)
{
#if CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SOFTAP
    return true;
#else
    return false;
#endif
}

static bool cap_eth_lan(void)
{
#if CONFIG_BRIDGE_DATA_FORWARDING_NETIF_ETHERNET
    return hw_w5500();
#else
    return false;
#endif
}

static bool cap_sta_wan(void)
{
#if CONFIG_BRIDGE_EXTERNAL_NETIF_STATION
    return true;
#else
    return false;
#endif
}

static bool cap_modem_wan(void)
{
    return hw_usb_lte();
}

static bool cap_eth_wan_softap(void)
{
#if CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET && !CONFIG_BRIDGE_DATA_FORWARDING_NETIF_ETHERNET && CONFIG_BRIDGE_DATA_FORWARDING_NETIF_SOFTAP
    return hw_w5500();
#else
    return false;
#endif
}

static bool work_mode_allowed(uint8_t id)
{
    switch (id) {
    case 1:
        return cap_modem_wan() && cap_softap();
    case 2:
        return cap_modem_wan() && cap_eth_lan();
    case 3:
        return cap_modem_wan() && cap_softap() && cap_eth_lan();
    case 4:
        return cap_sta_wan() && cap_softap();
    case 5:
        return cap_sta_wan() && cap_eth_lan();
    case 6:
        return cap_sta_wan() && cap_softap() && cap_eth_lan();
    case 7:
        return cap_eth_wan_softap();
    default:
        return false;
    }
}

static void mode_push(cJSON *arr, uint8_t id, const char *label, bool needs_sta)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", id);
    cJSON_AddStringToObject(o, "label", label);
    cJSON_AddBoolToObject(o, "needs_sta", needs_sta);
    cJSON_AddItemToArray(arr, o);
}

static cJSON *json_build_mode_list(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (work_mode_allowed(1)) {
        mode_push(arr, 1, "4G -> Wi-Fi (SoftAP)", false);
    }
    if (work_mode_allowed(2)) {
        mode_push(arr, 2, "4G -> W5500 (ETH_LAN)", false);
    }
    if (work_mode_allowed(3)) {
        mode_push(arr, 3, "4G -> Wi-Fi + W5500", false);
    }
    if (work_mode_allowed(4)) {
        mode_push(arr, 4, "Wi-Fi STA -> SoftAP", true);
    }
    if (work_mode_allowed(5)) {
        mode_push(arr, 5, "Wi-Fi STA -> W5500 (ETH_LAN)", true);
    }
    if (work_mode_allowed(6)) {
        mode_push(arr, 6, "Wi-Fi STA -> Wi-Fi + W5500", true);
    }
    if (work_mode_allowed(7)) {
        mode_push(arr, 7, "W5500 WAN -> SoftAP only", false);
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

static esp_err_t load_sta_from_nvs(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open("wifi_cfg", NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_get_str(nvs, "sta_ssid", ssid, &ssid_len);
    if (ret != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }
    ret = nvs_get_str(nvs, "sta_pwd", password, &password_len);
    nvs_close(nvs);
    return ret;
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

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

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

    /* Stop current STA connection attempts (NVS cleared, so after reset it won't auto-connect). */
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);

    return send_json(req, 200, "{\"status\":\"success\"}");
}

static esp_err_t uri_wifi_scan_get(httpd_req_t *req)
{
    const int max_aps = 20;
    const size_t json_cap = 3072;

    wifi_scan_config_t scan_cfg = {0};
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
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

    uint8_t mode = 0;
    if (load_work_mode_u8(&mode) == ESP_OK && mode <= 7) {
        cJSON_AddNumberToObject(root, "saved_work_mode", mode);
    } else {
        cJSON_AddNullToObject(root, "saved_work_mode");
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
    if (le == ESP_OK && cur >= 1 && cur <= 7) {
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
    if (!cJSON_IsNumber(mid) || mid->valuedouble < 1 || mid->valuedouble > 7) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"mode 1..7 required\"}");
    }
    uint8_t m = (uint8_t)mid->valuedouble;
    cJSON_Delete(root);

    if (!work_mode_allowed(m)) {
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"mode not allowed for this hardware/build\"}");
    }

    esp_err_t ret = save_work_mode_u8(m);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "save work_mode: %s", esp_err_to_name(ret));
        return send_json(req, 400, "{\"status\":\"error\",\"message\":\"nvs save failed\"}");
    }
    return send_json(req, 200, "{\"status\":\"success\",\"hint\":\"Saved. Full WAN/LAN switch may require menuconfig build + reboot; STA Wi-Fi still applied live.\"}");
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

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/www",
        .partition_label = "www",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "mount /www failed");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 24;
    /* Default ~4KB stack is too small for scan + LittleFS + TLS-style call depth */
    config.stack_size = 12288;
    config.task_priority = 5;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd start failed");
    ESP_RETURN_ON_ERROR(register_handlers(s_server), TAG, "register handlers failed");

    // If previous STA config exists, apply once on boot.
    char ssid[33] = {0};
    char pwd[65] = {0};
    if (load_sta_from_nvs(ssid, sizeof(ssid), pwd, sizeof(pwd)) == ESP_OK && strlen(ssid)) {
        esp_err_t ret = apply_sta_config(ssid, pwd);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Apply saved STA failed: %s", esp_err_to_name(ret));
        }
    }

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
