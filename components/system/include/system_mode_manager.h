/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSTEM_WAN_NONE = 0,
    SYSTEM_WAN_USB_MODEM = 1,
    SYSTEM_WAN_WIFI_STA = 2,
    SYSTEM_WAN_W5500 = 3,
} system_wan_type_t;

typedef struct {
    uint8_t id;
    const char *label;
    system_wan_type_t wan_type;
    bool lan_softap;
    bool lan_eth;
    bool needs_sta;
    bool needs_eth_wan_cfg;
} system_mode_profile_t;

typedef struct {
    uint8_t current_mode;
    uint8_t target_mode;
    uint8_t last_ok_mode;
    esp_err_t last_error;
    bool applying;
    bool rollback_last_apply;
    char phase[24];
} system_mode_status_t;

esp_err_t system_mode_manager_apply(uint8_t mode);
esp_err_t system_mode_manager_apply_saved(void);
/**
 * Log NVS `work_mode` (or absence) for startup diagnostics. Call after HW probe, before
 * `system_bridge_init_netifs_from_hw()`, then call `system_mode_manager_apply_saved_or_hw_default()`.
 */
void system_mode_manager_log_startup_plan(void);
/** Prefer saved NVS mode when allowed; else SoftAP-only provisioning (11), else HW-based default. */
esp_err_t system_mode_manager_apply_saved_or_hw_default(void);
/** Best-effort default mode id for current hardware (see profiles in system_mode_manager.c). */
uint8_t system_mode_manager_pick_hw_default_mode(void);
uint8_t system_mode_manager_current(void);
const system_mode_profile_t *system_mode_manager_get_profiles(size_t *out_count);
const system_mode_profile_t *system_mode_manager_get_profile(uint8_t mode);
bool system_mode_manager_mode_allowed(uint8_t mode);
void system_mode_manager_get_status(system_mode_status_t *out);

#ifdef __cplusplus
}
#endif

