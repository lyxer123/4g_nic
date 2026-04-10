#ifndef BLE_SETTINGS_H
#define BLE_SETTINGS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start BLE peripheral (Bluedroid GATT): vendor service 0xFF50, JSON command/response.
 * Safe to call once; later calls return ESP_OK without re-init.
 */
esp_err_t ble_settings_start(void);

#ifdef __cplusplus
}
#endif

#endif
