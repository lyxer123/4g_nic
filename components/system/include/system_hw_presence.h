/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Call once after esp_netif_init + default loop, before esp_bridge_create_all_netif(). */
void system_hw_presence_probe_before_bridge(void);

#ifdef __cplusplus
}
#endif
