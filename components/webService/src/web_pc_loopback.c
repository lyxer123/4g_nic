/**
 * Loopback HTTP client: same REST handlers as the browser, without duplicating JSON logic.
 * Used by UART PCAPI bridge (serial_cli).
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "web_service.h"

static const char *TAG = "web_pc_lb";

/** Default httpd listen port when CONFIG_HTTPD_SERVER_PORT is not in sdkconfig. */
#ifndef CONFIG_HTTPD_SERVER_PORT
#define WEB_PC_LOCAL_PORT 80
#else
#define WEB_PC_LOCAL_PORT CONFIG_HTTPD_SERVER_PORT
#endif

#define WEB_PC_RESP_MAX (512 * 1024)

typedef struct {
    char *buf;
    size_t len;
} web_pc_accum_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    web_pc_accum_t *a = (web_pc_accum_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        break;
    case HTTP_EVENT_ON_DATA:
        if (!a || !evt->data || evt->data_len <= 0) {
            break;
        }
        if (a->len + (size_t)evt->data_len > WEB_PC_RESP_MAX) {
            ESP_LOGW(TAG, "response truncated (max %u)", (unsigned)WEB_PC_RESP_MAX);
            break;
        }
        {
            char *nb = (char *)realloc(a->buf, a->len + (size_t)evt->data_len + 1);
            if (!nb) {
                ESP_LOGW(TAG, "oom accumulating response");
                break;
            }
            a->buf = nb;
            memcpy(a->buf + a->len, evt->data, (size_t)evt->data_len);
            a->len += (size_t)evt->data_len;
            a->buf[a->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t web_pc_loopback_http(const char *method,
                               const char *path,
                               const char *content_type,
                               const char *body,
                               size_t body_len,
                               int *status_code,
                               char **resp_out,
                               size_t *resp_len)
{
    ESP_RETURN_ON_FALSE(method && path && status_code && resp_out && resp_len, ESP_ERR_INVALID_ARG, TAG, "args");
    ESP_RETURN_ON_FALSE(path[0] == '/', ESP_ERR_INVALID_ARG, TAG, "path");

    if (!web_service_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[320];
    int urll = snprintf(url, sizeof(url), "http://127.0.0.1:%d%s", (int)WEB_PC_LOCAL_PORT, path);
    if (urll <= 0 || urll >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "url overflow");
        return ESP_ERR_INVALID_ARG;
    }

    web_pc_accum_t acc = {.buf = NULL, .len = 0};

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http_event,
        .user_data = &acc,
        .timeout_ms = 120000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(cl != NULL, ESP_ERR_NO_MEM, TAG, "init");

    char *post_copy = NULL;

    if (strcasecmp(method, "GET") == 0) {
        esp_http_client_set_method(cl, HTTP_METHOD_GET);
    } else if (strcasecmp(method, "POST") == 0) {
        esp_http_client_set_method(cl, HTTP_METHOD_POST);
        esp_http_client_set_header(cl, "Content-Type",
                                   (content_type && content_type[0]) ? content_type : "application/json; charset=utf-8");
        if (body && body_len > 0) {
            post_copy = (char *)malloc(body_len + 1u);
            if (!post_copy) {
                esp_http_client_cleanup(cl);
                return ESP_ERR_NO_MEM;
            }
            memcpy(post_copy, body, body_len);
            post_copy[body_len] = '\0';
            esp_http_client_set_post_field(cl, post_copy, (int)body_len);
        } else {
            esp_http_client_set_post_field(cl, "", 0);
        }
    } else if (strcasecmp(method, "DELETE") == 0) {
        esp_http_client_set_method(cl, HTTP_METHOD_DELETE);
    } else {
        esp_http_client_cleanup(cl);
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = esp_http_client_perform(cl);
    *status_code = esp_http_client_get_status_code(cl);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "perform %s %s: %s", method, path, esp_err_to_name(err));
        free(acc.buf);
        acc.buf = NULL;
        free(post_copy);
        esp_http_client_cleanup(cl);
        *resp_out = NULL;
        *resp_len = 0;
        return err;
    }

    esp_http_client_cleanup(cl);
    free(post_copy);

    *resp_out = acc.buf;
    *resp_len = acc.len;
    return ESP_OK;
}
