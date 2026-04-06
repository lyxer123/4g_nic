/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Probe USB LTE Cat.1 modem (e.g. ML307C) via USB host enumeration: device descriptor
 * VID/PID whitelist and optional CDC communication interface heuristic.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install USB host briefly at boot, enumerate, match table + Kconfig VID/PID.
 *
 * Intended before esp_bridge_create_all_netif(); does not depend on CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM.
 * When CONFIG_SYSTEM_USB_CAT1_DETECT is disabled, returns ESP_ERR_NOT_SUPPORTED (no-op for callers).
 */
esp_err_t system_usb_cat1_detect_run(void);

bool system_usb_cat1_detect_present(void);

/** Best-effort: last matched idVendor / idProduct (0 if unknown). */
void system_usb_cat1_detect_last_ids(uint16_t *vid, uint16_t *pid);

#ifdef __cplusplus
}
#endif
