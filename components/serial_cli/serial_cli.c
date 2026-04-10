/**
 * UART CLI: esp_console for command registry + esp_console_run(), no esp_console_new_repl_uart.
 * REPL + linenoise redirects stdout and can recurse with esp_log on the same UART (Guru / MMU fault).
 * This task reads lines with uart_read_bytes and writes replies with uart_write_bytes only.
 * See doc/命令.md
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

#ifndef CONFIG_HTTPD_MAX_URI_LEN
#define PCAPI_PATH_MAX 512
#else
#define PCAPI_PATH_MAX CONFIG_HTTPD_MAX_URI_LEN
#endif

#define PCAPI_LINE_MAX 640
#define PCAPI_POST_MAX (512 * 1024)
#define PCAPI_BODY_READ_MS 120000

static int cmd_help(int argc, char **argv);
static int cmd_ping(int argc, char **argv);
static int cmd_mode_get(int argc, char **argv);
static int cmd_mode_set(int argc, char **argv);
static int cmd_version(int argc, char **argv);

/** Find "PCAPI …" in line (e.g. after ESP log prefix); not inside identifier like "fooPCAPI". */
static const char *find_pcapi_rest(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        if (strncasecmp(p, "PCAPI", 5) != 0) {
            continue;
        }
        if (p != s) {
            unsigned char prev = (unsigned char)p[-1];
            if (isalnum(prev) || prev == '_') {
                continue;
            }
        }
        p += 5;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        return p;
    }
    return NULL;
}

static void pcapi_path_rtrim(char *path)
{
    if (!path) {
        return;
    }
    size_t n = strlen(path);
    while (n > 0 && (unsigned char)path[n - 1] <= 0x20u) {
        path[--n] = '\0';
    }
}

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

static void pcapi_send_hdr(int http_status, size_t body_len)
{
    /* New line before PCAPI_OUT so it is never glued to a previous incomplete log line. */
    cli_write_raw("\r\n", 2);
    char h[80];
    int n = snprintf(h, sizeof(h), "PCAPI_OUT %d %u\r\n", http_status, (unsigned)body_len);
    if (n > 0) {
        cli_write_raw(h, (size_t)n);
    }
}

static void pcapi_reply_err(int http_status, const char *json_msg)
{
    size_t sl = json_msg ? strlen(json_msg) : 0;
    pcapi_send_hdr(http_status, sl);
    if (sl) {
        cli_write_raw(json_msg, sl);
    }
}

static bool pcapi_parse_post_spec(const char *rest, char *path, size_t path_sz, size_t *body_len)
{
    const char *end = rest + strlen(rest);
    while (end > rest && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    const char *last_sp = NULL;
    for (const char *q = rest; q < end; q++) {
        if (*q == ' ') {
            last_sp = q;
        }
    }
    if (!last_sp || last_sp <= rest) {
        return false;
    }
    for (const char *q = last_sp + 1; q < end; q++) {
        if (*q < '0' || *q > '9') {
            return false;
        }
    }
    size_t plen = (size_t)(last_sp - rest);
    if (plen == 0 || plen >= path_sz) {
        return false;
    }
    memcpy(path, rest, plen);
    path[plen] = '\0';
    unsigned long n = strtoul(last_sp + 1, NULL, 10);
    if (n > (unsigned long)PCAPI_POST_MAX) {
        return false;
    }
    *body_len = (size_t)n;
    return true;
}

static esp_err_t read_uart_exact(uint8_t *dst, size_t len, int timeout_ms)
{
    if (timeout_ms < 1000) {
        timeout_ms = 1000;
    }
    size_t got = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (got < len) {
        if (xTaskGetTickCount() > deadline) {
            return ESP_ERR_TIMEOUT;
        }
        int r = uart_read_bytes(UART_CLI_NUM, dst + got, (int)(len - got), pdMS_TO_TICKS(50));
        if (r > 0) {
            got += (size_t)r;
        }
    }
    return ESP_OK;
}

/** Emit loopback JSON after HTTP; frees resp. */
static void pcapi_write_loopback_payload(int st, char *resp, size_t rlen)
{
    if (rlen > 0 && resp) {
        char hdr[80];
        int hl = snprintf(hdr, sizeof(hdr), "PCAPI_OUT %d %u\r\n", st, (unsigned)rlen);
        if (hl > 0) {
            size_t blob = 2U + (size_t)hl + rlen;
            char *one = (char *)malloc(blob);
            if (one) {
                memcpy(one, "\r\n", 2);
                memcpy(one + 2, hdr, (size_t)hl);
                memcpy(one + 2 + (size_t)hl, resp, rlen);
                cli_write_raw(one, blob);
                free(one);
            } else {
                pcapi_send_hdr(st, rlen);
                cli_write_raw(resp, rlen);
            }
        }
    } else {
        pcapi_send_hdr(st, 0);
    }
    free(resp);
}

/**
 * RX 偶尔丢失行首字节时可能只剩 "GET /api/..."。与完整 PCAPI 行等效（仅 GET）。
 */
static void handle_pcapi_bare_get(const char *path_start)
{
    char path[PCAPI_PATH_MAX + 8];
    size_t i = 0;
    while (path_start[i] && path_start[i] != ' ' && path_start[i] != '\t' && path_start[i] != '\r' && i < sizeof(path) - 1) {
        path[i] = path_start[i];
        i++;
    }
    path[i] = '\0';
    while (i > 0 && (path[i - 1] == ' ' || path[i - 1] == '\t')) {
        path[--i] = '\0';
    }
    if (path[0] != '/') {
        pcapi_reply_err(400, "{\"status\":\"error\",\"message\":\"path must start with /\"}");
        return;
    }
    int st = 503;
    char *resp = NULL;
    size_t rlen = 0;
    esp_err_t e = web_pc_loopback_http("GET", path, NULL, NULL, 0, &st, &resp, &rlen);
    if (e != ESP_OK) {
        char ebuf[160];
        snprintf(ebuf, sizeof(ebuf), "{\"status\":\"error\",\"message\":\"%s\"}", esp_err_to_name(e));
        pcapi_reply_err(503, ebuf);
        return;
    }
    pcapi_write_loopback_payload(st, resp, rlen);
}

/** p 指向 "GET|POST|DELETE ..."（已去掉 "PCAPI" 前缀） */
static void handle_pcapi_from(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    char path[PCAPI_PATH_MAX + 8];
    const char *method = NULL;
    char *body = NULL;
    size_t body_len = 0;

    if (strncasecmp(p, "GET ", 4) == 0) {
        method = "GET";
        const char *ps = p + 4;
        while (*ps == ' ' || *ps == '\t') {
            ps++;
        }
        strlcpy(path, ps, sizeof(path));
    } else if (strncasecmp(p, "DELETE ", 7) == 0) {
        method = "DELETE";
        const char *ps = p + 7;
        while (*ps == ' ' || *ps == '\t') {
            ps++;
        }
        strlcpy(path, ps, sizeof(path));
    } else if (strncasecmp(p, "POST ", 5) == 0) {
        method = "POST";
        const char *rest = p + 5;
        while (*rest == ' ' || *rest == '\t') {
            rest++;
        }
        if (!pcapi_parse_post_spec(rest, path, sizeof(path), &body_len)) {
            pcapi_reply_err(400, "{\"status\":\"error\",\"message\":\"bad PCAPI POST line\"}");
            return;
        }
        if (body_len > 0) {
            /* +1 for '\0': esp_http_client may use strlen(post_data) for Content-Length; without
             * terminator the declared length can be wrong and the last byte of JSON is dropped. */
            body = (char *)malloc(body_len + 1u);
            if (!body) {
                pcapi_reply_err(500, "{\"status\":\"error\",\"message\":\"oom\"}");
                return;
            }
            if (read_uart_exact((uint8_t *)body, body_len, PCAPI_BODY_READ_MS) != ESP_OK) {
                free(body);
                pcapi_reply_err(408, "{\"status\":\"error\",\"message\":\"post body read timeout\"}");
                return;
            }
            body[body_len] = '\0';
        }
    } else {
        pcapi_reply_err(400, "{\"status\":\"error\",\"message\":\"use PCAPI GET|POST|DELETE\"}");
        return;
    }

    pcapi_path_rtrim(path);

    if (path[0] != '/') {
        free(body);
        pcapi_reply_err(400, "{\"status\":\"error\",\"message\":\"path must start with /\"}");
        return;
    }

    int st = 503;
    char *resp = NULL;
    size_t rlen = 0;
    esp_err_t e = web_pc_loopback_http(method, path, NULL, body, body_len, &st, &resp, &rlen);
    free(body);
    body = NULL;

    if (e != ESP_OK) {
        char ebuf[160];
        snprintf(ebuf, sizeof(ebuf), "{\"status\":\"error\",\"message\":\"%s\"}", esp_err_to_name(e));
        pcapi_reply_err(503, ebuf);
        return;
    }
    pcapi_write_loopback_payload(st, resp, rlen);
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

    /* RX 加大：WiFi 扫描时日志多，避免冲掉下一行 PCAPI 命令头 */
    esp_err_t e = uart_driver_install(UART_CLI_NUM, 16384, 4096, 0, NULL, 0);
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

    char line[PCAPI_LINE_MAX];
    cli_write("\r\n4g_nic uart cli — type 'help' or PCAPI lines for PC admin\r\n4g_nic> ");

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
            char *s = line;
            while (*s == ' ' || *s == '\t' || *s == '\r') {
                s++;
            }
            const char *rest = find_pcapi_rest(s);
            if (rest) {
                handle_pcapi_from(rest);
            } else if (strncasecmp(s, "GET ", 4) == 0) {
                const char *ps = s + 4;
                while (*ps == ' ' || *ps == '\t') {
                    ps++;
                }
                if (*ps == '/') {
                    handle_pcapi_bare_get(ps);
                } else {
                    int ret = 0;
                    esp_err_t er = esp_console_run(s, &ret);
                    if (er == ESP_ERR_NOT_FOUND) {
                        cli_write("ERR unknown command (try help)\r\n");
                    } else if (er != ESP_OK) {
                        cli_printf("ERR run %s\r\n", esp_err_to_name(er));
                    }
                    (void)ret;
                }
            } else {
                int ret = 0;
                esp_err_t er = esp_console_run(s, &ret);
                if (er == ESP_ERR_NOT_FOUND) {
                    cli_write("ERR unknown command (try help)\r\n");
                } else if (er != ESP_OK) {
                    cli_printf("ERR run %s\r\n", esp_err_to_name(er));
                }
                (void)ret;
            }
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

    if (xTaskCreate(uart_cli_task, "uart_cli", 12288, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate uart_cli failed");
        return;
    }
    ESP_LOGI(TAG, "UART line CLI task started (no REPL; avoids log/uart reentrancy)");
}
