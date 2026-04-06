/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB LTE modem presence: usb_host enumeration, VID/PID whitelist (+ Kconfig extra pair),
 * optional CDC class heuristic. Runs at boot before bridge netifs; independent of
 * CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM (for UI / logs when uplink is STA or ETH only).
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "system_usb_cat1_detect.h"

#if CONFIG_SYSTEM_USB_CAT1_DETECT

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"

static const char *TAG = "usb_cat1_det";

static bool s_present;
static uint16_t s_vid;
static uint16_t s_pid;

typedef struct {
    uint16_t vid;
    uint16_t pid;
} usb_vid_pid_t;

/* ML307C USB composition: VID 0x2ecc, PID 0x3012.
 * Keep in sync with menuconfig: Bridge → USB Configuration → BRIDGE_MODEM_USB_VID / _PID
 * (when CONFIG_BRIDGE_SERIAL_VIA_USB + modem netif are enabled). Project sdkconfig.old
 * documents this pair; active sdkconfig may omit them if modem is disabled. IoT-Bridge
 * Kconfig defaults are Quectel BG96 (0x2C7C/0x0296) — override there for ML307C. */
static const usb_vid_pid_t s_known_lte[] = {
    {0x2ecc, 0x3012},
};

typedef struct {
    volatile bool new_dev;
} usb_probe_evt_t;

static usb_probe_evt_t s_evt;

#define USB_CAT1_PROBE_MAX_SEEN 8
static uint16_t s_seen_vid[USB_CAT1_PROBE_MAX_SEEN];
static uint16_t s_seen_pid[USB_CAT1_PROBE_MAX_SEEN];
static int s_seen_n;

static void usb_seen_reset(void)
{
    s_seen_n = 0;
}

static void cat1_assert_enable_gpio(void)
{
#if CONFIG_SYSTEM_USB_CAT1_ENABLE_GPIO >= 0
    const int pin = CONFIG_SYSTEM_USB_CAT1_ENABLE_GPIO;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    const int level = CONFIG_SYSTEM_USB_CAT1_ENABLE_ACTIVE_HIGH ? 1 : 0;
    gpio_set_level((gpio_num_t)pin, level);
    ESP_LOGI(TAG, "modem enable GPIO%d = %d, wait %d ms before USB host", pin, level,
             CONFIG_SYSTEM_USB_CAT1_ENABLE_SETTLE_MS);
    if (CONFIG_SYSTEM_USB_CAT1_ENABLE_SETTLE_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SYSTEM_USB_CAT1_ENABLE_SETTLE_MS));
    }
#else
    /* No enable GPIO: module must be self-powered or always-on. */
#endif
}

static void usb_seen_note(uint16_t vid, uint16_t pid)
{
    for (int i = 0; i < s_seen_n; i++) {
        if (s_seen_vid[i] == vid && s_seen_pid[i] == pid) {
            return;
        }
    }
    if (s_seen_n < USB_CAT1_PROBE_MAX_SEEN) {
        s_seen_vid[s_seen_n] = vid;
        s_seen_pid[s_seen_n] = pid;
        s_seen_n++;
    }
}

static bool vid_pid_in_table(uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < sizeof(s_known_lte) / sizeof(s_known_lte[0]); i++) {
        if (s_known_lte[i].vid == vid && s_known_lte[i].pid == pid) {
            return true;
        }
    }
#if CONFIG_SYSTEM_USB_CAT1_EXTRA_VID != 0 && CONFIG_SYSTEM_USB_CAT1_EXTRA_PID != 0
    if (vid == CONFIG_SYSTEM_USB_CAT1_EXTRA_VID && pid == CONFIG_SYSTEM_USB_CAT1_EXTRA_PID) {
        return true;
    }
#endif
    return false;
}

static bool config_has_cdc_modem_like(const usb_config_desc_t *cfg)
{
    if (!cfg) {
        return false;
    }
    bool has_comm = false;
    bool has_data = false;
    int offset = 0;
    const usb_standard_desc_t *next = (const usb_standard_desc_t *)cfg;

    while ((next = usb_parse_next_descriptor_of_type(next, cfg->wTotalLength, USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                                      &offset)) != NULL) {
        const usb_intf_desc_t *intf = (const usb_intf_desc_t *)next;
        if (intf->bInterfaceClass == USB_CLASS_COMM) {
            has_comm = true;
        }
        if (intf->bInterfaceClass == USB_CLASS_CDC_DATA) {
            has_data = true;
        }
    }
    return has_comm && has_data;
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    usb_probe_evt_t *ctx = (usb_probe_evt_t *)arg;
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        ctx->new_dev = true;
    }
}

static esp_err_t probe_devices(usb_host_client_handle_t client)
{
    uint8_t addrs[8];
    int num = 0;
    esp_err_t err = usb_host_device_addr_list_fill((int)sizeof(addrs), addrs, &num);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < num; i++) {
        usb_device_handle_t dev = NULL;
        err = usb_host_device_open(client, addrs[i], &dev);
        if (err != ESP_OK || dev == NULL) {
            continue;
        }

        const usb_device_desc_t *dd = NULL;
        err = usb_host_get_device_descriptor(dev, &dd);
        if (err != ESP_OK || dd == NULL) {
            usb_host_device_close(client, dev);
            continue;
        }

        uint16_t vid = dd->idVendor;
        uint16_t pid = dd->idProduct;
        usb_seen_note(vid, pid);
        bool table_hit = vid_pid_in_table(vid, pid);

        const usb_config_desc_t *cfg = NULL;
        bool cdc_ok = false;
        if (usb_host_get_active_config_descriptor(dev, &cfg) == ESP_OK && cfg) {
            cdc_ok = config_has_cdc_modem_like(cfg);
        }

#if CONFIG_SYSTEM_USB_CAT1_ACCEPT_ANY_CDC
        const bool accept = table_hit || cdc_ok;
#else
        const bool accept = table_hit;
#endif
        if (accept) {
            s_present = true;
            s_vid = vid;
            s_pid = pid;
            ESP_LOGI(TAG, "USB LTE modem candidate: VID=0x%04x PID=0x%04x (table=%d cdc=%d)", vid, pid, table_hit,
                     cdc_ok);
            usb_host_device_close(client, dev);
            return ESP_OK;
        }

        ESP_LOGD(TAG, "skip addr=%u VID=0x%04x PID=0x%04x (not in Cat1 table)", (unsigned)addrs[i], vid, pid);
        usb_host_device_close(client, dev);
    }

    return ESP_OK;
}

esp_err_t system_usb_cat1_detect_run(void)
{
    s_present = false;
    s_vid = 0;
    s_pid = 0;
    usb_seen_reset();
    memset(&s_evt, 0, sizeof(s_evt));

    cat1_assert_enable_gpio();

    /* PHY + root hub are initialized here (ESP-IDF usb_host). This is unrelated to IoT-Bridge:
     * CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM only gates esp_bridge_create_modem_netif() (esp_modem DTE).
     * CONFIG_BRIDGE_DATA_FORWARDING_NETIF_USB gates TinyUSB *device* stack in bridge_usb.c.
     * Neither runs before this probe (see app_main: probe runs before esp_bridge_create_all_netif()). */
    usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t err = usb_host_install(&host_cfg);
    const bool we_installed = (err == ESP_OK);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "USB host already installed; skip probe");
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
        return err;
    }

    usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = &s_evt,
        },
    };

    usb_host_client_handle_t client = NULL;
    err = usb_host_client_register(&client_cfg, &client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register: %s", esp_err_to_name(err));
        usb_host_uninstall();
        return err;
    }

    /* Let VBUS / modem PHY settle after host install (many Cat1 need > reset recovery time). */
    vTaskDelay(pdMS_TO_TICKS(150));

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CONFIG_SYSTEM_USB_CAT1_ENUM_TIMEOUT_MS);

    while (xTaskGetTickCount() < deadline) {
        uint32_t fl = 0;
        (void)usb_host_lib_handle_events(pdMS_TO_TICKS(30), &fl);
        (void)usb_host_client_handle_events(client, pdMS_TO_TICKS(30));

        /* Always scan: lib_info num_devices can lag behind the address list after connect events. */
        (void)probe_devices(client);
        if (s_present) {
            break;
        }
        if (s_evt.new_dev) {
            s_evt.new_dev = false;
        }
    }

    if (!s_present) {
        (void)probe_devices(client);
    }

    if (!s_present) {
        if (s_seen_n == 0) {
            usb_host_lib_info_t finfo = {0};
            int lib_ndev = -1;
            if (usb_host_lib_info(&finfo) == ESP_OK) {
                lib_ndev = finfo.num_devices;
            }
            ESP_LOGW(TAG,
                     "No USB device opened in %d ms (host lib num_devices=%d). Check USB OTG D+/D-/VBUS/GND; "
                     "slow Cat1: increase CONFIG_SYSTEM_USB_CAT1_ENUM_TIMEOUT_MS; same PHY cannot run TinyUSB "
                     "device stack together with usb_host.",
                     CONFIG_SYSTEM_USB_CAT1_ENUM_TIMEOUT_MS, lib_ndev);
        } else {
            char line[128];
            int pos = snprintf(line, sizeof(line), "USB enumerated %d device(s), none matched Cat1 whitelist: ", s_seen_n);
            for (int i = 0; i < s_seen_n && pos < (int)sizeof(line) - 20; i++) {
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%s0x%04x:0x%04x", i ? ", " : "",
                                s_seen_vid[i], s_seen_pid[i]);
            }
            ESP_LOGW(TAG, "%s (menuconfig: System options -> extra VID/PID or ACCEPT_ANY_CDC)", line);
        }
    }

    err = usb_host_client_deregister(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_client_deregister: %s", esp_err_to_name(err));
    }

    for (int i = 0; i < 30; i++) {
        uint32_t fl = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &fl);
        if (fl & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            break;
        }
    }
    (void)usb_host_device_free_all();
    for (int i = 0; i < 30; i++) {
        uint32_t fl = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &fl);
        if (fl & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            break;
        }
    }

    if (we_installed) {
        err = usb_host_uninstall();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_uninstall: %s", esp_err_to_name(err));
        }
    }

    return ESP_OK;
}

bool system_usb_cat1_detect_present(void)
{
    return s_present;
}

void system_usb_cat1_detect_last_ids(uint16_t *vid, uint16_t *pid)
{
    if (vid) {
        *vid = s_vid;
    }
    if (pid) {
        *pid = s_pid;
    }
}

#else /* !CONFIG_SYSTEM_USB_CAT1_DETECT */

esp_err_t system_usb_cat1_detect_run(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool system_usb_cat1_detect_present(void)
{
    return false;
}

void system_usb_cat1_detect_last_ids(uint16_t *vid, uint16_t *pid)
{
    if (vid) {
        *vid = 0;
    }
    if (pid) {
        *pid = 0;
    }
}

#endif /* CONFIG_SYSTEM_USB_CAT1_DETECT */
