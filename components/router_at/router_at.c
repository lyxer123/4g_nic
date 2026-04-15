/*
 * Router AT: small ESP-AT-like subset for an external MCU, aligned with this firmware (not byte-compatible).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <strings.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_bridge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "router_at.h"
#include "system_w5500_detect.h"
#include "system_usb_cat1_detect.h"
#include "web_service.h"

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

static void at_write_fmt(void (*write_bytes)(const void *, size_t), const char *fmt, ...)
{
    if (!write_bytes || !fmt) {
        return;
    }
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        write_bytes(buf, (size_t)len);
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

static bool get_netif_ip_info(const char *key, esp_netif_ip_info_t *out)
{
    if (!out) {
        return false;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(key);
    if (!netif) {
        return false;
    }
    if (esp_netif_get_ip_info(netif, out) != ESP_OK) {
        return false;
    }
    return out->ip.addr != 0;
}

static bool http_probe_url(const char *url, const char *host_hdr, int *out_code, esp_err_t *out_err)
{
    if (!url || !out_code || !out_err) {
        return false;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 7000,
        .addr_type = HTTP_ADDR_TYPE_INET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        *out_err = ESP_FAIL;
        *out_code = 0;
        return false;
    }
    if (host_hdr) {
        esp_http_client_set_header(client, "Host", host_hdr);
    }
    *out_err = esp_http_client_perform(client);
    *out_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (*out_err == ESP_OK && *out_code > 0);
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
                 "\r\n+CMD:AT,ATE0,ATE1,AT+GMR,AT+RST,AT+CMD,AT+SYSRAM,AT+ROUTER,AT+PING,AT+MODE,AT+MODEMINFO,AT+W5500,AT+W5500IP,AT+USB4G,AT+USB4GIP,AT+NETCHECK\r\n");
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "PING") == 0) {
        at_write(write_bytes, "\r\n+PING:ok cmd=ping device=4g_nic\r\n");
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "MODE") == 0) {
        const char *q = sub;
        while (*q && *q != '?' && *q != '=' && *q != ' ' && *q != '\t') {
            q++;
        }
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (*q == '?' || *q == '\0') {
            uint8_t m = 0;
            esp_err_t e = web_service_get_work_mode_u8(&m);
            if (e != ESP_OK) {
                at_error(write_bytes);
                return;
            }
            at_write_fmt(write_bytes, "\r\n+MODE:work_mode_id=%u\r\n", (unsigned)m);
            at_ok(write_bytes);
            return;
        }
        if (*q == '=') {
            q++;
            skip_sp(&q);
            if (*q == '\0') {
                at_error(write_bytes);
                return;
            }
            char *endptr = NULL;
            long v = strtol(q, &endptr, 10);
            if (endptr == q || v < 0 || v > 255) {
                at_error(write_bytes);
                return;
            }
            esp_err_t e = web_service_apply_work_mode_id((uint8_t)v);
            if (e != ESP_OK) {
                at_error(write_bytes);
                return;
            }
            at_write_fmt(write_bytes, "\r\n+MODE:ok work_mode_id=%u\r\n", (unsigned)v);
            at_ok(write_bytes);
            return;
        }
        at_error(write_bytes);
        return;
    }

#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM)
    if (strcmp(name, "MODEMINFO") == 0) {
        esp_bridge_modem_info_t mi;
        esp_err_t e = esp_bridge_modem_get_info(&mi);
        if (e != ESP_OK || e == ESP_ERR_INVALID_STATE || !mi.present) {
            at_error(write_bytes);
            return;
        }
        at_write_fmt(write_bytes,
                     "\r\n+MODEMINFO:ppp_has_ip=%d,iccid=%s,imsi=%s,imei=%s,operator=%s,network_mode=%s,act=%d,rssi=%d,ber=%d,manufacturer=%s,model=%s,fw_version=%s\r\n",
                     mi.ppp_has_ip ? 1 : 0,
                     mi.iccid,
                     mi.imsi,
                     mi.imei,
                     mi.operator_name,
                     mi.network_mode,
                     mi.act,
                     mi.rssi,
                     mi.ber,
                     mi.manufacturer,
                     mi.module_name,
                     mi.fw_version);
        at_ok(write_bytes);
        return;
    }
#endif

    if (strcmp(name, "W5500") == 0) {
        bool present = system_w5500_detect_present();
        uint8_t version = system_w5500_detect_version_raw();
        at_write_fmt(write_bytes, "\r\n+W5500:present=%d,version=0x%02x\r\n", present ? 1 : 0,
                     version);
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "W5500IP") == 0) {
        esp_netif_ip_info_t ip_info;
        bool ok = get_netif_ip_info("ETH_WAN", &ip_info);
        if (!ok) {
            ok = get_netif_ip_info("ETH_LAN", &ip_info);
        }
        if (!ok) {
            at_error(write_bytes);
            return;
        }
        at_write_fmt(write_bytes, "\r\n+W5500IP:ip=" IPSTR ",mask=" IPSTR ",gateway=" IPSTR "\r\n",
                     IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "USB4G") == 0) {
        bool present = system_usb_cat1_detect_present();
        uint16_t vid = 0, pid = 0;
        system_usb_cat1_detect_last_ids(&vid, &pid);
        at_write_fmt(write_bytes, "\r\n+USB4G:present=%d,vid=0x%04x,pid=0x%04x\r\n", present ? 1 : 0,
                     vid, pid);
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "USB4GIP") == 0) {
        esp_netif_ip_info_t ip_info;
        if (!get_netif_ip_info("PPP_DEF", &ip_info)) {
            at_error(write_bytes);
            return;
        }
        at_write_fmt(write_bytes, "\r\n+USB4GIP:ip=" IPSTR ",mask=" IPSTR ",gateway=" IPSTR "\r\n",
                     IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
        at_ok(write_bytes);
        return;
    }

    if (strcmp(name, "NETCHECK") == 0) {
        int status = 0;
        esp_err_t err = ESP_OK;
        bool ok = http_probe_url("http://www.baidu.com/", NULL, &status, &err);
        if (ok) {
            at_write_fmt(write_bytes, "\r\n+NETCHECK:ok,method=http,target=www.baidu.com,status=%d\r\n", status);
            at_ok(write_bytes);
            return;
        }
        at_write_fmt(write_bytes, "\r\n+NETCHECK:fail,method=http,target=www.baidu.com,err=%s,status=%d\r\n",
                     esp_err_to_name(err), status);
        at_error(write_bytes);
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
