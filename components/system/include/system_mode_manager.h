/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t system_mode_manager_apply(uint8_t mode);
esp_err_t system_mode_manager_apply_saved(void);
uint8_t system_mode_manager_current(void);

#ifdef __cplusplus
}
#endif

