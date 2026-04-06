/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Probe W5500 presence by reading VERSIONR (0x0039) before esp-iot-bridge attaches SPI Ethernet.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run SPI probe: init bus, optional PHY RST, read chip version byte (expect 0x04).
 *
 * Must run before esp_bridge_create_all_netif() so SPI bus is free. Safe to call when
 * CONFIG_ETH_SPI_ETHERNET_W5500 is disabled (returns ESP_ERR_NOT_SUPPORTED).
 */
esp_err_t system_w5500_detect_run(void);

/** Last run: true if VERSIONR read as W5500_CHIP_VERSION (0x04). */
bool system_w5500_detect_present(void);

/** Last successful read raw version register (valid if detect returned ESP_OK). */
uint8_t system_w5500_detect_version_raw(void);

#ifdef __cplusplus
}
#endif
