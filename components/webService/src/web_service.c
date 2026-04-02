#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_bridge.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "web_service.h"

static const char *TAG = "web_service";
static httpd_handle_t s_server = NULL;
static bool s_running = false;

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

    ESP_RETURN_ON_ERROR(esp_bridge_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set sta config failed");
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

    httpd_uri_t wifi_scan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = uri_wifi_scan_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wifi_scan), TAG, "register wifi scan failed");

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
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "mount /www failed");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
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
