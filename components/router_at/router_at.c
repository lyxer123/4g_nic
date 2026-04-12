/*
 * Router AT: small ESP-AT-like subset for an external MCU, aligned with this firmware (not byte-compatible).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "router_at.h"

static const char *TAG = "router_at";

#if defined(CONFIG_ROUTER_AT_ENABLE) && CONFIG_ROUTER_AT_ENABLE

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#endif

#define ROUTER_AT_LINE_MAX 512

static uint8_t s_effective_uart_num;
static bool s_shared_with_console;
static int s_ate_echo; /* 0 = off, 1 = on (line echo before result) */

static void trim_tail(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && ((unsigned char)s[n - 1] <= 0x20u)) {
        s[--n] = '\0';
    }
}

static void skip_sp(const char **p)
{
    while (**p == ' ' || **p == '\t') {
        (*p)++;
    }
}

static bool looks_like_at_line(const char *line)
{
    const char *p = line;
    skip_sp(&p);
    if (strncasecmp(p, "AT", 2) != 0) {
        return false;
    }
    if (p[2] == '\0') {
        return true;
    }
    if (p[2] == '+' || p[2] == '&') {
        return true;
    }
    if (strncasecmp(p, "ATE", 3) == 0) {
        return true;
    }
    return false;
}

static void at_write(void (*write_bytes)(const void *, size_t), const char *s)
{
    if (write_bytes && s) {
        write_bytes(s, strlen(s));
    }
}

static void at_ok(void (*write_bytes)(const void *, size_t))
{
    at_write(write_bytes, "\r\nOK\r\n");
}

static void at_error(void (*write_bytes)(const void *, size_t))
{
    at_write(write_bytes, "\r\nERROR\r\n");
}

static void maybe_echo_line(void (*write_bytes)(const void *, size_t), const char *line)
{
    if (!write_bytes || !s_ate_echo) {
        return;
    }
    at_write(write_bytes, line);
    at_write(write_bytes, "\r\n");
}

static bool parse_plus_name(const char *after_plus, char *out_sub, size_t out_sz)
{
    const char *q = after_plus;
    while (*q == ' ' || *q == '\t') {
        q++;
    }
    size_t i = 0;
    while (*q && *q != '?' && *q != '=' && *q != ' ' && *q != '\t') {
        if (i + 1 >= out_sz) {
            return false;
        }
        out_sub[i++] = (char)toupper((unsigned char)*q);
        q++;
    }
    out_sub[i] = '\0';
    return i > 0;
}

static void handle_at_line_inner(const char *line_raw, void (*write_bytes)(const void *, size_t))
{
    char buf[ROUTER_AT_LINE_MAX];
    strlcpy(buf, line_raw, sizeof(buf));
    trim_tail(buf);

    const char *p = buf;
    skip_sp(&p);

    if (strncasecmp(p, "AT", 2) != 0) {
        at_error(write_bytes);
        return;
    }

    maybe_echo_line(write_bytes, buf);

    /* Plain AT */
    if (p[2] == '\0') {
        at_ok(write_bytes);
        return;
    }

    /* ATE0 / ATE1 */
    if (strncasecmp(p, "ATE0", 4) == 0 && (p[4] == '\0' || p[4] == ' ')) {
        s_ate_echo = 0;
        at_ok(write_bytes);
        return;
    }
    if (strncasecmp(p, "ATE1", 4) == 0 && (p[4] == '\0' || p[4] == ' ')) {
        s_ate_echo = 1;
        at_ok(write_bytes);
        return;
    }

    if (p[2] != '+') {
        at_error(write_bytes);
        return;
    }

    const char *sub = p + 3;
    char name[24];
    if (!parse_plus_name(sub, name, sizeof(name))) {
        at_error(write_bytes);
        return;
    }

    if (strcmp(name, "GMR") == 0) {
        const esp_app_desc_t *d = esp_app_get_description();
        char verbuf[192];
        snprintf(verbuf, sizeof(verbuf), "\r\nAT version:%s\r\nSDK:%s\r\n", d->version, esp_get_idf_version());
        at_write(write_bytes, verbuf);
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "RST") == 0) {
        at_ok(write_bytes);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        return;
    }

    if (strcmp(name, "CMD") == 0) {
        at_write(write_bytes,
                 "\r\n+CMD:AT,ATE0,ATE1,AT+GMR,AT+RST,AT+CMD,AT+SYSRAM,AT+ROUTER\r\n");
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "SYSRAM") == 0) {
        size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        char line[96];
        snprintf(line, sizeof(line), "\r\n+SYSRAM:internal=%u,dma=%u\r\n", (unsigned)internal, (unsigned)dma);
        at_write(write_bytes, line);
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "ROUTER") == 0) {
        int baud = s_shared_with_console ? CONFIG_ESP_CONSOLE_UART_BAUDRATE : CONFIG_ROUTER_AT_UART_BAUD;
        char out[160];
        snprintf(out, sizeof(out),
                 "\r\n+ROUTER:uart=%u,shared=%u,baud=%d,project=4g_nic\r\n", (unsigned)s_effective_uart_num,
                 s_shared_with_console ? 1u : 0u, baud);
        at_write(write_bytes, out);
        at_ok(write_bytes);
        return;
    }

    at_error(write_bytes);
}

static uint8_t resolve_uart_num_from_config_and_nvs(void)
{
    uint8_t u = (uint8_t)CONFIG_ROUTER_AT_UART_NUM;
    nvs_handle_t h;
    if (nvs_open("router_at", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 255;
        if (nvs_get_u8(h, "uart_num", &v) == ESP_OK && v <= 2) {
            u = v;
        }
        nvs_close(h);
    }
    return u;
}

bool router_at_is_shared_with_console(void)
{
    return s_shared_with_console;
}

uint8_t router_at_effective_uart_num(void)
{
    return s_effective_uart_num;
}

esp_err_t router_at_set_uart_num_nvs(uint8_t uart_num)
{
    if (uart_num > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t e = nvs_open("router_at", NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_u8(h, "uart_num", uart_num);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

bool router_at_try_handle_line(const char *line, void (*write_bytes)(const void *, size_t))
{
    if (!line || !write_bytes) {
        return false;
    }
    if (!s_shared_with_console) {
        return false;
    }
    if (!looks_like_at_line(line)) {
        return false;
    }
    handle_at_line_inner(line, write_bytes);
    return true;
}

static void uart_task_write(const void *d, size_t n)
{
    (void)uart_write_bytes((uart_port_t)s_effective_uart_num, d, n);
}

static void router_at_uart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));

    char line[ROUTER_AT_LINE_MAX];
    for (;;) {
        int nread = 0;
        while (nread < (int)sizeof(line) - 1) {
            uint8_t ch = 0;
            int r = uart_read_bytes((uart_port_t)s_effective_uart_num, &ch, 1, portMAX_DELAY);
            if (r != 1) {
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                break;
            }
            if (ch == 0x7f || ch == '\b') {
                if (nread > 0) {
                    nread--;
                }
                continue;
            }
            line[nread++] = (char)ch;
        }
        line[nread] = '\0';
        trim_tail(line);
        if (nread <= 0) {
            continue;
        }
        if (!looks_like_at_line(line)) {
            /* Dedicated UART: only AT protocol */
            at_write(uart_task_write, "\r\nERROR\r\n");
            continue;
        }
        handle_at_line_inner(line, uart_task_write);
    }
}

static esp_err_t install_dedicated_uart(void)
{
    uart_port_t port = (uart_port_t)s_effective_uart_num;
    if (uart_is_driver_installed(port)) {
        ESP_LOGW(TAG, "UART%u driver already installed; reusing for AT", (unsigned)port);
        return ESP_OK;
    }

    uart_config_t ucfg = {
        .baud_rate = CONFIG_ROUTER_AT_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t e = uart_driver_install(port, 4096, 1024, 0, NULL, 0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    e = uart_param_config(port, &ucfg);
    if (e != ESP_OK) {
        return e;
    }
#if CONFIG_ROUTER_AT_UART_TX_GPIO >= 0 && CONFIG_ROUTER_AT_UART_RX_GPIO >= 0
    e = uart_set_pin(port, CONFIG_ROUTER_AT_UART_TX_GPIO, CONFIG_ROUTER_AT_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE);
#else
    ESP_LOGW(TAG, "AT UART GPIO not set (TX/RX -1); ensure pins are configured elsewhere");
    e = ESP_OK;
#endif
    return e;
}

void router_at_start(void)
{
    s_effective_uart_num = resolve_uart_num_from_config_and_nvs();
    s_shared_with_console = (s_effective_uart_num == (uint8_t)CONFIG_ESP_CONSOLE_UART_NUM);

    ESP_LOGI(TAG, "AT UART effective=%u (config=%d nvs_override), shared_with_console=%d", (unsigned)s_effective_uart_num,
             CONFIG_ROUTER_AT_UART_NUM, s_shared_with_console ? 1 : 0);

    if (s_shared_with_console) {
        ESP_LOGI(TAG, "AT shares console UART: lines starting with AT go to router AT handler");
        return;
    }

    esp_err_t e = install_dedicated_uart();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "dedicated AT UART install failed: %s", esp_err_to_name(e));
        return;
    }

    if (xTaskCreate(router_at_uart_task, "router_at", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "router_at task create failed");
    }
}

#else /* !CONFIG_ROUTER_AT_ENABLE */

void router_at_start(void) {}

bool router_at_try_handle_line(const char *line, void (*write_bytes)(const void *, size_t))
{
    (void)line;
    (void)write_bytes;
    return false;
}

uint8_t router_at_effective_uart_num(void)
{
    return 0;
}

bool router_at_is_shared_with_console(void)
{
    return true;
}

esp_err_t router_at_set_uart_num_nvs(uint8_t uart_num)
{
    (void)uart_num;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_ROUTER_AT_ENABLE */
