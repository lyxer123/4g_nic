#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_service_start(void);
esp_err_t web_service_stop(void);
bool web_service_is_running(void);

/** After bridge Wi‑Fi init: restore SoftAP SSID/password from NVS (driver uses WIFI_STORAGE_RAM). */
void web_softap_restore_from_nvs(void);

/** Read persisted work mode id from NVS (`bridge_ui` / `work_mode`). */
esp_err_t web_service_get_work_mode_u8(uint8_t *out);

/**
 * Validate mode, persist NVS, schedule deferred `system_mode_manager_apply`, update web_ui keys.
 * Same effect as setting `work_mode_id` in POST `/api/network/config`.
 */
esp_err_t web_service_apply_work_mode_id(uint8_t mode_id);

/**
 * Issue HTTP to the local httpd (127.0.0.1), same handlers as the Web UI.
 * @param content_type optional for POST (defaults to application/json)
 * @param body POST body or NULL / body_len 0
 * @param status_code HTTP status from device (e.g. 200)
 * @param resp_out malloc'd response body (may be empty); caller must free()
 * @param resp_len response length in bytes
 */
esp_err_t web_pc_loopback_http(const char *method,
                               const char *path,
                               const char *content_type,
                               const char *body,
                               size_t body_len,
                               int *status_code,
                               char **resp_out,
                               size_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif
