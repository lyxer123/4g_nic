#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start AT subsystem: dedicated UART task when AT port != console UART; otherwise hooks only
 * router_at_try_handle_line() from serial_cli.
 */
void router_at_start(void);

/**
 * When AT shares the console UART, try to handle one trimmed line as AT.
 * @return true if the line was consumed (caller should not run CLI / PCAPI).
 */
bool router_at_try_handle_line(const char *line, void (*write_bytes)(const void *data, size_t len));

/** Effective AT UART index after Kconfig + optional NVS override (0..2). */
uint8_t router_at_effective_uart_num(void);

/** True when AT uses the same UART number as CONFIG_ESP_CONSOLE_UART_NUM. */
bool router_at_is_shared_with_console(void);

/**
 * Persist UART port (0..2) in NVS namespace "router_at", key "uart_num".
 * Takes effect after reboot. Use for PC tools / manufacturing without reflashing.
 */
esp_err_t router_at_set_uart_num_nvs(uint8_t uart_num);

#ifdef __cplusplus
}
#endif
