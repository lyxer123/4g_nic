#pragma once

#include "esp_err.h"

/**
 * Optional: periodic heap / minimum-free-heap logging; helps spot slow leaks under load.
 * Controlled by CONFIG_SYSTEM_STABILITY_HEAP_LOG_INTERVAL_S (0 = disabled).
 */
esp_err_t system_stability_init(void);
