/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Optional replacement for esp_bridge_create_all_netif(): create bridge netifs
 * based on boot-time hardware probes (W5500 / USB Cat.1).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create IoT-Bridge netifs in the same order as esp_bridge_create_all_netif(),
 * but skip SPI Ethernet / modem PPP when probes report absence (reduces init
 * noise and avoids dangling interfaces).
 *
 * Requires system_hw_presence_probe_before_bridge() already run.
 */
void system_bridge_init_netifs_from_hw(void);

#ifdef __cplusplus
}
#endif
