/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_modem_c_api_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_modem_api.h"
#include "esp_log.h"
#include "esp_bridge.h"
#include "esp_bridge_internal.h"
#include "sdkconfig.h"

#define MODULE_BOOT_TIME_MS     5000
#ifndef CONFIG_ESP_MODEM_C_API_STR_MAX
#define CONFIG_ESP_MODEM_C_API_STR_MAX 128
#endif
#if defined(CONFIG_BRIDGE_FLOW_CONTROL_NONE)
#define BRIDGE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_NONE
#elif defined(CONFIG_BRIDGE_FLOW_CONTROL_SW)
#define BRIDGE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_SW
#elif defined(CONFIG_BRIDGE_FLOW_CONTROL_HW)
#define BRIDGE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_HW
#endif

#if CONFIG_BRIDGE_MODEM_DEVICE_BG96 == 1
#define ESP_BRIDGE_MODEM_DEVICE         ESP_MODEM_DCE_BG96
#elif CONFIG_BRIDGE_MODEM_DEVICE_SIM800 == 1
#define ESP_BRIDGE_MODEM_DEVICE         ESP_MODEM_DCE_SIM800
#elif CONFIG_BRIDGE_MODEM_DEVICE_SIM7000 == 1
#define ESP_BRIDGE_MODEM_DEVICE         ESP_MODEM_DCE_SIM7000
#elif CONFIG_BRIDGE_MODEM_DEVICE_SIM7070 == 1
#define ESP_BRIDGE_MODEM_DEVICE         ESP_MODEM_DCE_SIM7070
#elif CONFIG_BRIDGE_MODEM_DEVICE_SIM7600 == 1
#define ESP_BRIDGE_MODEM_DEVICE         ESP_MODEM_DCE_SIM7600
#endif

static const char *TAG = "bridge_modem";
static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int USB_DISCONNECTED_BIT = BIT3; // Used only with USB DTE but we define it unconditionally, to avoid too many #ifdefs in the code
static esp_modem_dce_t *s_dce = NULL;
static SemaphoreHandle_t s_modem_lock;

static void modem_info_reset(esp_bridge_modem_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->rssi = 99;
    info->ber = 99;
    info->act = -1;
    snprintf(info->operator_name, sizeof(info->operator_name), "--");
    snprintf(info->network_mode, sizeof(info->network_mode), "--");
    snprintf(info->imei, sizeof(info->imei), "--");
    snprintf(info->imsi, sizeof(info->imsi), "--");
    snprintf(info->iccid, sizeof(info->iccid), "--");
    snprintf(info->module_name, sizeof(info->module_name), "--");
    snprintf(info->manufacturer, sizeof(info->manufacturer), "--");
    snprintf(info->fw_version, sizeof(info->fw_version), "--");
}

static const char *act_to_mode(int act)
{
    switch (act) {
    case 0: return "GSM";
    case 2: return "WCDMA";
    case 7: return "LTE";
    default: return "--";
    }
}

static char s_iccid_parse_buf[32];
static int s_cops_act = -1;

static const char *decode_cn_operator(const char *code)
{
    if (!code || code[0] == '\0') {
        return NULL;
    }
    if (strncmp(code, "46000", 5) == 0 || strncmp(code, "46002", 5) == 0 ||
        strncmp(code, "46004", 5) == 0 || strncmp(code, "46007", 5) == 0 ||
        strncmp(code, "46008", 5) == 0 || strncmp(code, "46013", 5) == 0 ||
        strncmp(code, "898600", 6) == 0 || strncmp(code, "898602", 6) == 0) {
        return "\xe4\xb8\xad\xe5\x9b\xbd\xe7\xa7\xbb\xe5\x8a\xa8"; /* 中国移动 */
    }
    if (strncmp(code, "46001", 5) == 0 || strncmp(code, "46006", 5) == 0 ||
        strncmp(code, "46009", 5) == 0 || strncmp(code, "46010", 5) == 0 ||
        strncmp(code, "898601", 6) == 0) {
        return "\xe4\xb8\xad\xe5\x9b\xbd\xe8\x81\x94\xe9\x80\x9a"; /* 中国联通 */
    }
    if (strncmp(code, "46003", 5) == 0 || strncmp(code, "46005", 5) == 0 ||
        strncmp(code, "46011", 5) == 0 || strncmp(code, "898603", 6) == 0) {
        return "\xe4\xb8\xad\xe5\x9b\xbd\xe7\x94\xb5\xe4\xbf\xa1"; /* 中国电信 */
    }
    return NULL;
}

static void try_fill_operator_from_sim(esp_bridge_modem_info_t *info)
{
    if (!info) {
        return;
    }
    if (strcmp(info->operator_name, "--") != 0) {
        return;
    }
    const char *op = decode_cn_operator(info->imsi);
    if (!op) {
        op = decode_cn_operator(info->iccid);
    }
    if (op) {
        snprintf(info->operator_name, sizeof(info->operator_name), "%s", op);
    }
}

static esp_err_t iccid_line_cb(uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_OK;
    }
    /* Prefer ICCID-like runs (18–22 digits); ignore short spurious numbers on a line. */
    size_t best_len = 0;
    size_t best_start = 0;
    for (size_t i = 0; i < len; ) {
        if (data[i] >= '0' && data[i] <= '9') {
            size_t j = i;
            while (j < len && data[j] >= '0' && data[j] <= '9') {
                j++;
            }
            size_t run = j - i;
            if (run >= 18 && run <= 22 && run > best_len) {
                best_len = run;
                best_start = i;
            }
            i = j;
        } else {
            i++;
        }
    }
    if (best_len == 0) {
        int start = -1;
        int end = -1;
        for (size_t i = 0; i < len; i++) {
            if (data[i] >= '0' && data[i] <= '9') {
                if (start < 0) {
                    start = (int)i;
                }
                end = (int)i;
            } else if (start >= 0) {
                break;
            }
        }
        if (start >= 0 && end >= start) {
            best_len = (size_t)(end - start + 1);
            best_start = (size_t)start;
        }
    }
    if (best_len > 0 && best_len < sizeof(s_iccid_parse_buf)) {
        memcpy(s_iccid_parse_buf, data + best_start, best_len);
        s_iccid_parse_buf[best_len] = '\0';
    }
    return ESP_OK;
}

static void extract_iccid_from_text(const char *text, char *out, size_t out_sz)
{
    if (!text || !out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    size_t best_len = 0;
    const char *best = NULL;
    for (const char *p = text; *p; ) {
        if (*p >= '0' && *p <= '9') {
            const char *s = p;
            size_t L = 0;
            while (p[0] >= '0' && p[0] <= '9') {
                L++;
                p++;
            }
            if (L >= 18 && L <= 22 && L > best_len) {
                best_len = L;
                best = s;
            }
        } else {
            p++;
        }
    }
    if (best && best_len > 0) {
        size_t cpy = best_len < out_sz - 1 ? best_len : out_sz - 1;
        memcpy(out, best, cpy);
        out[cpy] = '\0';
    }
}

static void trim_at_response_line(char *s)
{
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

static esp_err_t try_iccid_at(esp_modem_dce_t *dce, const char *cmd, char *out, size_t out_sz)
{
    char buf[CONFIG_ESP_MODEM_C_API_STR_MAX];
    if (esp_modem_at(dce, cmd, buf, 4000) != ESP_OK) {
        return ESP_FAIL;
    }
    trim_at_response_line(buf);
    extract_iccid_from_text(buf, out, out_sz);
    return (out[0] != '\0') ? ESP_OK : ESP_FAIL;
}

static esp_err_t cops_line_cb(uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_OK;
    }
    for (size_t i = 0; i < len; i++) {
        if (data[i] == ',') {
            int v = 0;
            bool has = false;
            for (size_t j = i + 1; j < len; j++) {
                if (data[j] >= '0' && data[j] <= '9') {
                    has = true;
                    v = v * 10 + (data[j] - '0');
                } else if (has) {
                    s_cops_act = v;
                    return ESP_OK;
                }
            }
            if (has) {
                s_cops_act = v;
            }
            return ESP_OK;
        }
    }
    return ESP_OK;
}

esp_err_t esp_bridge_modem_get_info(esp_bridge_modem_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    modem_info_reset(info);
    if (!s_dce) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_modem_lock) {
        s_modem_lock = xSemaphoreCreateMutex();
    }
    if (!s_modem_lock || xSemaphoreTake(s_modem_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    info->present = true;

    esp_netif_t *ppp = esp_netif_get_handle_from_ifkey("PPP_DEF");
    if (ppp) {
        esp_netif_ip_info_t ip = {0};
        if (esp_netif_get_ip_info(ppp, &ip) == ESP_OK && ip.ip.addr != 0) {
            info->ppp_has_ip = true;
        }
    }

    (void)esp_modem_pause_net(s_dce, true);

    char buf[64] = {0};
    int act = -1;
    if (esp_modem_get_operator_name(s_dce, buf, &act) == ESP_OK && buf[0] != '\0') {
        snprintf(info->operator_name, sizeof(info->operator_name), "%.*s",
                 (int)sizeof(info->operator_name) - 1, buf);
        info->act = act;
        snprintf(info->network_mode, sizeof(info->network_mode), "%s", act_to_mode(act));
    }
    memset(buf, 0, sizeof(buf));
    if (esp_modem_get_imei(s_dce, buf) == ESP_OK && buf[0] != '\0') {
        snprintf(info->imei, sizeof(info->imei), "%.*s", (int)sizeof(info->imei) - 1, buf);
    }
    memset(buf, 0, sizeof(buf));
    if (esp_modem_get_imsi(s_dce, buf) == ESP_OK && buf[0] != '\0') {
        snprintf(info->imsi, sizeof(info->imsi), "%.*s", (int)sizeof(info->imsi) - 1, buf);
    }
    memset(buf, 0, sizeof(buf));
    if (esp_modem_get_module_name(s_dce, buf) == ESP_OK && buf[0] != '\0') {
        snprintf(info->module_name, sizeof(info->module_name), "%.*s",
                 (int)sizeof(info->module_name) - 1, buf);
    }
    /* Manufacturer / revision: generic DCE does not always map these; ML307 supports standard AT+CGMI/AT+CGMR. */
    {
        char mb[CONFIG_ESP_MODEM_C_API_STR_MAX];
        if (esp_modem_at(s_dce, "AT+CGMI", mb, 4000) == ESP_OK && mb[0] != '\0') {
            trim_at_response_line(mb);
            if (mb[0] != '\0') {
                snprintf(info->manufacturer, sizeof(info->manufacturer), "%.*s",
                         (int)sizeof(info->manufacturer) - 1, mb);
            }
        }
        if (strcmp(info->manufacturer, "--") == 0 && esp_modem_at(s_dce, "ATI", mb, 4000) == ESP_OK && mb[0] != '\0') {
            trim_at_response_line(mb);
            if (mb[0] != '\0') {
                snprintf(info->manufacturer, sizeof(info->manufacturer), "%.*s",
                         (int)sizeof(info->manufacturer) - 1, mb);
            }
        }
        if (esp_modem_at(s_dce, "AT+CGMR", mb, 4000) == ESP_OK && mb[0] != '\0') {
            trim_at_response_line(mb);
            if (mb[0] != '\0') {
                snprintf(info->fw_version, sizeof(info->fw_version), "%.*s",
                         (int)sizeof(info->fw_version) - 1, mb);
            }
        }
        if (strcmp(info->fw_version, "--") == 0 && esp_modem_at(s_dce, "AT+GMR", mb, 4000) == ESP_OK && mb[0] != '\0') {
            trim_at_response_line(mb);
            if (mb[0] != '\0') {
                snprintf(info->fw_version, sizeof(info->fw_version), "%.*s",
                         (int)sizeof(info->fw_version) - 1, mb);
            }
        }
    }
    int rssi = 99, ber = 99;
    if (esp_modem_get_signal_quality(s_dce, &rssi, &ber) == ESP_OK) {
        info->rssi = rssi;
        info->ber = ber;
    }
    /* ICCID: esp_modem_at() appends \\r; esp_modem_command() does not — must use \\r in the string. */
    if (try_iccid_at(s_dce, "AT+MCCID", info->iccid, sizeof(info->iccid)) != ESP_OK &&
        try_iccid_at(s_dce, "AT+MCCID?", info->iccid, sizeof(info->iccid)) != ESP_OK &&
        try_iccid_at(s_dce, "AT+ICCID", info->iccid, sizeof(info->iccid)) != ESP_OK &&
        try_iccid_at(s_dce, "AT+CCID", info->iccid, sizeof(info->iccid)) != ESP_OK &&
        try_iccid_at(s_dce, "AT+QCCID", info->iccid, sizeof(info->iccid)) != ESP_OK) {
        memset(s_iccid_parse_buf, 0, sizeof(s_iccid_parse_buf));
        if (esp_modem_command(s_dce, "AT+MCCID\r", iccid_line_cb, 4000) != ESP_OK || s_iccid_parse_buf[0] == '\0') {
            memset(s_iccid_parse_buf, 0, sizeof(s_iccid_parse_buf));
            if (esp_modem_command(s_dce, "AT+MCCID?\r", iccid_line_cb, 4000) != ESP_OK || s_iccid_parse_buf[0] == '\0') {
                memset(s_iccid_parse_buf, 0, sizeof(s_iccid_parse_buf));
                if (esp_modem_command(s_dce, "AT+ICCID\r", iccid_line_cb, 4000) != ESP_OK || s_iccid_parse_buf[0] == '\0') {
                    memset(s_iccid_parse_buf, 0, sizeof(s_iccid_parse_buf));
                    (void)esp_modem_command(s_dce, "AT+CCID\r", iccid_line_cb, 4000);
                }
            }
        }
        if (s_iccid_parse_buf[0] == '\0') {
            memset(s_iccid_parse_buf, 0, sizeof(s_iccid_parse_buf));
            (void)esp_modem_command(s_dce, "AT+QCCID\r", iccid_line_cb, 4000);
        }
        if (s_iccid_parse_buf[0] != '\0') {
            snprintf(info->iccid, sizeof(info->iccid), "%.*s", (int)sizeof(info->iccid) - 1, s_iccid_parse_buf);
        }
    }
    if (strcmp(info->network_mode, "--") == 0) {
        s_cops_act = -1;
        if (esp_modem_command(s_dce, "AT+COPS?\r", cops_line_cb, 4000) == ESP_OK && s_cops_act >= 0) {
            snprintf(info->network_mode, sizeof(info->network_mode), "%s", act_to_mode(s_cops_act));
            info->act = s_cops_act;
        }
    }
    try_fill_operator_from_sim(info);

    (void)esp_modem_pause_net(s_dce, false);
    xSemaphoreGive(s_modem_lock);
    return ESP_OK;
}

#if (defined(CONFIG_BRIDGE_SERIAL_VIA_USB)) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))
#include "esp_modem_usb_c_api.h"
#include "esp_modem_usb_config.h"
#include "freertos/task.h"
static void usb_terminal_error_handler(esp_modem_terminal_error_t err)
{
    if (err == ESP_MODEM_TERMINAL_DEVICE_GONE) {
        ESP_LOGI(TAG, "USB modem disconnected");
        assert(event_group);
        xEventGroupSetBits(event_group, USB_DISCONNECTED_BIT);
    }
}
#endif

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %"PRId32"", event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", netif);
    }
}

static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %"PRId32"", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);
        esp_bridge_update_dns_info(event->esp_netif, NULL);
        ESP_LOGI(TAG, "GOT ip event!!!");
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
        IOT_BRIDGE_NAPT_TABLE_CLEAR();
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}

esp_netif_t *esp_bridge_create_modem_netif(esp_netif_ip_info_t *custom_ip_info, uint8_t custom_mac[6], bool data_forwarding, bool enable_dhcps)
{
    esp_netif_t *netif = NULL;

    if (data_forwarding || enable_dhcps) {
        return netif;
    }

    ESP_LOGW(TAG, "Force reset 4g board");
    gpio_config_t io_config = {
        .pin_bit_mask = BIT64(CONFIG_MODEM_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&io_config);
    gpio_set_level(CONFIG_MODEM_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(CONFIG_MODEM_RESET_GPIO, 1);

    vTaskDelay(pdMS_TO_TICKS(MODULE_BOOT_TIME_MS));

    event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    /* Configure the PPP netif */
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_BRIDGE_MODEM_PPP_APN);
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif);

    /* Configure the DTE */
#if defined(CONFIG_BRIDGE_SERIAL_VIA_UART)
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    /* setup UART specific configuration based on kconfig options */
    dte_config.uart_config.tx_io_num = CONFIG_BRIDGE_MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = CONFIG_BRIDGE_MODEM_UART_RX_PIN;
    dte_config.uart_config.rts_io_num = CONFIG_BRIDGE_MODEM_UART_RTS_PIN;
    dte_config.uart_config.cts_io_num = CONFIG_BRIDGE_MODEM_UART_CTS_PIN;
    dte_config.uart_config.baud_rate = CONFIG_BRIDGE_MODEM_BAUD_RATE;
    dte_config.uart_config.flow_control = BRIDGE_FLOW_CONTROL;
    dte_config.uart_config.rx_buffer_size = CONFIG_BRIDGE_MODEM_UART_RX_BUFFER_SIZE;
    dte_config.uart_config.tx_buffer_size = CONFIG_BRIDGE_MODEM_UART_TX_BUFFER_SIZE;
    dte_config.uart_config.event_queue_size = CONFIG_BRIDGE_MODEM_UART_EVENT_QUEUE_SIZE;
    dte_config.task_stack_size = CONFIG_BRIDGE_MODEM_UART_EVENT_TASK_STACK_SIZE;
    dte_config.task_priority = CONFIG_BRIDGE_MODEM_UART_EVENT_TASK_PRIORITY;
    dte_config.dte_buffer_size = CONFIG_BRIDGE_MODEM_UART_RX_BUFFER_SIZE / 2;

#ifdef ESP_BRIDGE_MODEM_DEVICE
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_BRIDGE_MODEM_DEVICE, &dte_config, &dce_config, esp_netif);
#else
    ESP_LOGI(TAG, "Initializing esp_modem for a generic module...");
    esp_modem_dce_t *dce = esp_modem_new(&dte_config, &dce_config, esp_netif);
#endif

    assert(dce);
    if (dte_config.uart_config.flow_control == ESP_MODEM_FLOW_CONTROL_HW) {
        esp_err_t err = esp_modem_set_flow_control(dce, 2, 2);  //2/2 means HW Flow Control.
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set the set_flow_control mode");
            esp_modem_destroy(dce);
            esp_netif_destroy(esp_netif);
            vEventGroupDelete(event_group);
            event_group = NULL;
            return NULL;
        }
        ESP_LOGI(TAG, "HW set_flow_control OK");
    }

#elif (defined(CONFIG_BRIDGE_SERIAL_VIA_USB)) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))

    ESP_LOGI(TAG, "Initializing esp_modem for the BG96 module...");
    struct esp_modem_usb_term_config usb_config = ESP_MODEM_DEFAULT_USB_CONFIG(CONFIG_BRIDGE_MODEM_USB_VID, CONFIG_BRIDGE_MODEM_USB_PID, CONFIG_BRIDGE_MODEM_USB_INTERFACE_NUMBER); // VID, PID and interface num of 4G modem
    const esp_modem_dte_config_t dte_usb_config = ESP_MODEM_DTE_DEFAULT_USB_CONFIG(usb_config);
    ESP_LOGI(TAG, "Waiting for USB device connection...");
    //esp_modem_dce_t *dce = esp_modem_new_dev_usb(ESP_MODEM_DCE_BG96, &dte_usb_config, &dce_config, esp_netif);
    
    esp_modem_dce_t *dce = esp_modem_new_dev_usb(ESP_MODEM_DCE_GENETIC, &dte_usb_config, &dce_config, esp_netif);
    
    
    assert(dce);
    esp_modem_set_error_cb(dce, usb_terminal_error_handler);
    vTaskDelay(pdMS_TO_TICKS(2000)); // Although the DTE should be ready after USB enumeration, sometimes it fails to respond without this delay

#else
#error Invalid serial connection to modem.
#endif

    xEventGroupClearBits(event_group, CONNECT_BIT | USB_DISCONNECTED_BIT);

#if CONFIG_BRIDGE_NEED_SIM_PIN == 1
    // check if PIN needed
    bool pin_ok = false;
    if (esp_modem_read_pin(dce, &pin_ok) == ESP_OK && pin_ok == false) {
        if (esp_modem_set_pin(dce, CONFIG_BRIDGE_SIM_PIN) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            abort();
        }
    }
#endif

    int rssi, ber;
    esp_err_t err;
    int retry = 3;
    while (retry-- > 0) {
        err = esp_modem_get_signal_quality(dce, &rssi, &ber);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGE(TAG, "esp_modem_get_signal_quality failed with %d %s, retrying... (%d attempts left)", err, esp_err_to_name(err), retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_get_signal_quality failed after 3 attempts with %d %s", err, esp_err_to_name(err));
        esp_modem_destroy(dce);
        esp_netif_destroy(esp_netif);
        vEventGroupDelete(event_group);
        event_group = NULL;
        return NULL;
    }
    ESP_LOGI(TAG, "Signal quality: rssi=%d, ber=%d", rssi, ber);

    err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_set_mode(ESP_MODEM_MODE_DATA) failed with %d", err);
        esp_modem_destroy(dce);
        esp_netif_destroy(esp_netif);
        vEventGroupDelete(event_group);
        event_group = NULL;
        return NULL;
    }
    /* Wait for IP address */
    ESP_LOGI(TAG, "Waiting for IP address");
    xEventGroupWaitBits(event_group, CONNECT_BIT | USB_DISCONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    s_dce = dce;
    return esp_netif;
}
