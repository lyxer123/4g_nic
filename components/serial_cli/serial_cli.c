/**
 * UART CLI: esp_console for command registry + esp_console_run(), no esp_console_new_repl_uart.
 * REPL + linenoise redirects stdout and can recurse with esp_log on the same UART (Guru / MMU fault).
 * This task reads lines with uart_read_bytes and writes replies with uart_write_bytes only.
 * See doc/命令.md
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "serial_cli.h"
#include "web_service.h"

static const char *TAG = "serial_cli";

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#endif

#define UART_CLI_NUM ((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM)

static int cmd_help(int argc, char **argv);
static int cmd_ping(int argc, char **argv);
static int cmd_mode_get(int argc, char **argv);
static int cmd_mode_set(int argc, char **argv);
static int cmd_version(int argc, char **argv);

static const esp_console_cmd_t s_cmds[] = {
    {.command = "help", .help = "List commands", .hint = NULL, .func = cmd_help, .argtable = NULL},
    {.command = "ping", .help = "Alive check", .hint = NULL, .func = cmd_ping, .argtable = NULL},
    {.command = "mode_get", .help = "Print NVS work_mode_id", .hint = NULL, .func = cmd_mode_get, .argtable = NULL},
    {.command = "mode_set", .help = "Set work_mode_id", .hint = "<id>", .func = cmd_mode_set, .argtable = NULL},
    {.command = "version", .help = "App version", .hint = NULL, .func = cmd_version, .argtable = NULL},
};

static void cli_write_raw(const char *s, size_t len)
{
    if (!s || len == 0) {
        return;
    }
    (void)uart_write_bytes(UART_CLI_NUM, s, len);
}

static void cli_write(const char *s)
{
    if (s) {
        cli_write_raw(s, strlen(s));
    }
}

static void cli_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cli_write(buf);
}

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cli_write("commands: help ping mode_get mode_set <id> version\r\n");
    return 0;
}

static int cmd_ping(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cli_write("ok cmd=ping device=4g_nic\r\n");
    return 0;
}

static int cmd_mode_get(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t m = 0;
    esp_err_t e = web_service_get_work_mode_u8(&m);
    if (e != ESP_OK) {
        cli_printf("ERR nvs %s\r\n", esp_err_to_name(e));
        return 1;
    }
    cli_printf("ok work_mode_id=%u\r\n", (unsigned)m);
    return 0;
}

static int cmd_mode_set(int argc, char **argv)
{
    if (argc < 2) {
        cli_write("usage: mode_set <0-255>\r\n");
        return 1;
    }
    int v = atoi(argv[1]);
    if (v < 0 || v > 255) {
        cli_write("ERR invalid id\r\n");
        return 1;
    }
    esp_err_t e = web_service_apply_work_mode_id((uint8_t)v);
    if (e != ESP_OK) {
        cli_printf("ERR apply %s\r\n", esp_err_to_name(e));
        return 1;
    }
    cli_printf("ok cmd=set_mode work_mode_id=%u\r\n", (unsigned)v);
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_app_desc_t *d = esp_app_get_description();
    cli_printf("ok version=%s\r\n", d->version);
    return 0;
}

/**
 * Logs can reach UART0 via ROM/early printf without uart_driver_install(); that path has no RX queue.
 * uart_read_bytes() requires the full driver — install once if missing (ESP_ERR_INVALID_STATE = race OK).
 */
static esp_err_t uart_cli_ensure_driver(void)
{
    if (uart_is_driver_installed(UART_CLI_NUM)) {
        return ESP_OK;
    }

    uart_config_t ucfg = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t e = uart_driver_install(UART_CLI_NUM, 4096, 2048, 0, NULL, 0);
    if (e == ESP_ERR_INVALID_STATE) {
        return uart_is_driver_installed(UART_CLI_NUM) ? ESP_OK : e;
    }
    if (e != ESP_OK) {
        return e;
    }

    e = uart_param_config(UART_CLI_NUM, &ucfg);
    if (e != ESP_OK) {
        return e;
    }

#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    e = uart_set_pin(UART_CLI_NUM, CONFIG_ESP_CONSOLE_UART_TX_GPIO, CONFIG_ESP_CONSOLE_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE);
#endif
    return e;
}

static void uart_cli_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t ue = uart_cli_ensure_driver();
    if (ue != ESP_OK) {
        ESP_LOGE(TAG, "UART%u: %s (need CONFIG_ESP_CONSOLE_UART_NUM and pins for your board)", (unsigned)UART_CLI_NUM,
                 esp_err_to_name(ue));
        vTaskDelete(NULL);
        return;
    }

    char line[256];
    cli_write("\r\n4g_nic uart cli — type 'help'\r\n4g_nic> ");

    for (;;) {
        int nread = 0;
        while (nread < (int)sizeof(line) - 1) {
            uint8_t ch = 0;
            int r = uart_read_bytes(UART_CLI_NUM, &ch, 1, portMAX_DELAY);
            if (r != 1) {
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                break;
            }
            if (ch == 0x7f || ch == '\b') {
                if (nread > 0) {
                    nread--;
                    cli_write("\b \b");
                }
                continue;
            }
            line[nread++] = (char)ch;
        }
        line[nread] = '\0';

        while (nread > 0 && (line[nread - 1] == ' ' || line[nread - 1] == '\t')) {
            line[--nread] = '\0';
        }

        if (nread > 0) {
            int ret = 0;
            esp_err_t er = esp_console_run(line, &ret);
            if (er == ESP_ERR_NOT_FOUND) {
                cli_write("ERR unknown command (try help)\r\n");
            } else if (er != ESP_OK) {
                cli_printf("ERR run %s\r\n", esp_err_to_name(er));
            }
            (void)ret;
        }
        cli_write("4g_nic> ");
    }
}

void serial_cli_start(void)
{
    esp_console_config_t console_cfg = ESP_CONSOLE_CONFIG_DEFAULT();
    console_cfg.max_cmdline_length = 256;
    esp_err_t err = esp_console_init(&console_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_console_init: %s", esp_err_to_name(err));
        return;
    }

    for (size_t i = 0; i < sizeof(s_cmds) / sizeof(s_cmds[0]); i++) {
        err = esp_console_cmd_register(&s_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s: %s", s_cmds[i].command, esp_err_to_name(err));
            return;
        }
    }

    if (xTaskCreate(uart_cli_task, "uart_cli", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate uart_cli failed");
        return;
    }
    ESP_LOGI(TAG, "UART line CLI task started (no REPL; avoids log/uart reentrancy)");
}
