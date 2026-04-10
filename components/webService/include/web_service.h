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

#ifdef __cplusplus
}
#endif

#endif
