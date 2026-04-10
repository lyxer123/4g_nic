/**
 * BLE GATT channel for lightweight JSON commands (Bluedroid, same stack as sdkconfig).
 * Service 0xFF50: RX 0xFF51 (write), TX 0xFF52 (read + notify).
 */

#include <string.h>

#include "ble_settings.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "web_service.h"

static const char *TAG = "ble_settings";

#define BLE_PROFILE_APP_ID 0

#define SVC_UUID       0xFF50
#define CHAR_RX_UUID   0xFF51
#define CHAR_TX_UUID   0xFF52

/** Service + RX (decl+val) + TX (decl+val+CCCD). */
#define GATTS_NUM_HANDLE 8

static struct {
    uint16_t service_handle;
    uint16_t rx_handle;
    uint16_t tx_handle;
    uint16_t tx_cccd_handle;
    uint16_t conn_id;
    esp_gatt_if_t gatts_if;
    bool connected;
} s_prof;

#define BLE_DEVICE_NAME "4G_NIC_CFG"

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x40,
    .adv_int_max = 0x80,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint16_t s_mtu = ESP_GATT_DEF_BLE_MTU_SIZE;
static bool s_started;
static char s_notify_buf[512];

static uint8_t s_adv_svc_uuid_le[] = {0x50, 0xff};

/** Primary advertising data: flags + 16-bit UUID only (fits 31-byte legacy AD). */
static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp = false,
    .include_name = false,
    .include_txpower = false,
    .min_interval = 0xFFFF,
    .max_interval = 0xFFFF,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(s_adv_svc_uuid_le),
    .p_service_uuid = s_adv_svc_uuid_le,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

/** Scan response: device name only (separate 31-byte budget). */
static esp_ble_adv_data_t s_scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0xFFFF,
    .max_interval = 0xFFFF,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = 0,
};

static void ble_notify_json(const char *json)
{
    if (!s_prof.connected || s_prof.tx_handle == 0 || !json) {
        return;
    }
    size_t len = strlen(json);
    size_t max_payload = (s_mtu > 3) ? (size_t)(s_mtu - 3) : 20;
    if (len > max_payload) {
        ESP_LOGW(TAG, "response truncated mtu=%u len=%u", (unsigned)s_mtu, (unsigned)len);
        len = max_payload;
    }
    memcpy(s_notify_buf, json, len);
    s_notify_buf[len] = '\0';
    esp_err_t e = esp_ble_gatts_send_indicate(s_prof.gatts_if, s_prof.conn_id, s_prof.tx_handle, (uint16_t)len,
                                              (uint8_t *)s_notify_buf, false);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "send_indicate: %s", esp_err_to_name(e));
    }
}

static void handle_cmd(const char *body)
{
    char resp[sizeof(s_notify_buf)];
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"invalid_json\"}");
        ble_notify_json(resp);
        return;
    }
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd) || !cmd->valuestring) {
        cJSON_Delete(root);
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"missing_cmd\"}");
        ble_notify_json(resp);
        return;
    }
    const char *c = cmd->valuestring;
    if (strcmp(c, "ping") == 0) {
        cJSON_Delete(root);
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"cmd\":\"ping\",\"device\":\"4g_nic\"}");
        ble_notify_json(resp);
        return;
    }
    if (strcmp(c, "get_mode") == 0) {
        uint8_t m = 0;
        esp_err_t e = web_service_get_work_mode_u8(&m);
        cJSON_Delete(root);
        if (e != ESP_OK) {
            snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"nvs\",\"esp\":%d}", (int)e);
        } else {
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"work_mode_id\":%u}", (unsigned)m);
        }
        ble_notify_json(resp);
        return;
    }
    if (strcmp(c, "set_mode") == 0) {
        cJSON *wmid = cJSON_GetObjectItem(root, "work_mode_id");
        if (!cJSON_IsNumber(wmid)) {
            cJSON_Delete(root);
            snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"missing_work_mode_id\"}");
            ble_notify_json(resp);
            return;
        }
        uint8_t m = (uint8_t)wmid->valuedouble;
        esp_err_t er = web_service_apply_work_mode_id(m);
        cJSON_Delete(root);
        if (er != ESP_OK) {
            snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"apply_failed\",\"esp\":%d}", (int)er);
        } else {
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"cmd\":\"set_mode\",\"work_mode_id\":%u}", (unsigned)m);
        }
        ble_notify_json(resp);
        return;
    }
    if (strcmp(c, "version") == 0) {
        const esp_app_desc_t *d = esp_app_get_description();
        cJSON_Delete(root);
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"version\":\"%s\"}", d->version);
        ble_notify_json(resp);
        return;
    }
    cJSON_Delete(root);
    snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"unknown_cmd\"}");
    ble_notify_json(resp);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        /* Load scan response (name) then start advertising on SCAN_RSP complete. */
        if (esp_ble_gap_config_adv_data(&s_scan_rsp_data) != ESP_OK) {
            ESP_LOGE(TAG, "config_scan_rsp failed");
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&s_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "adv start failed");
        }
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t e, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (e) {
    case ESP_GATTS_REG_EVT:
        if (param->reg.app_id != BLE_PROFILE_APP_ID || param->reg.status != ESP_GATT_OK) {
            break;
        }
        s_prof.gatts_if = gatts_if;
        if (esp_ble_gap_set_device_name(BLE_DEVICE_NAME) != ESP_OK) {
            ESP_LOGE(TAG, "set_device_name failed");
        }
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id =
                {
                    .inst_id = 0,
                    .uuid =
                        {
                            .len = ESP_UUID_LEN_16,
                            .uuid = {.uuid16 = SVC_UUID},
                        },
                },
        };
        if (esp_ble_gatts_create_service(gatts_if, &service_id, GATTS_NUM_HANDLE) != ESP_OK) {
            ESP_LOGE(TAG, "create_service failed");
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        if (param->create.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "CREATE_EVT status=%d", (int)param->create.status);
            break;
        }
        s_prof.service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(s_prof.service_handle);

        esp_bt_uuid_t rx_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = CHAR_RX_UUID}};
        esp_ble_gatts_add_char(s_prof.service_handle, &rx_uuid, ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR, NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        if (param->add_char.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "ADD_CHAR_EVT status=%d", (int)param->add_char.status);
            break;
        }
        if (param->add_char.char_uuid.len == ESP_UUID_LEN_16 &&
            param->add_char.char_uuid.uuid.uuid16 == CHAR_RX_UUID) {
            s_prof.rx_handle = param->add_char.attr_handle;
            esp_bt_uuid_t tx_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = CHAR_TX_UUID}};
            esp_ble_gatts_add_char(s_prof.service_handle, &tx_uuid, ESP_GATT_PERM_READ,
                                   ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, NULL, NULL);
        } else if (param->add_char.char_uuid.len == ESP_UUID_LEN_16 &&
                   param->add_char.char_uuid.uuid.uuid16 == CHAR_TX_UUID) {
            s_prof.tx_handle = param->add_char.attr_handle;
            esp_bt_uuid_t descr_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}};
            esp_ble_gatts_add_char_descr(s_prof.service_handle, &descr_uuid,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        if (param->add_char_descr.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "ADD_CHAR_DESCR_EVT status=%d", (int)param->add_char_descr.status);
            break;
        }
        s_prof.tx_cccd_handle = param->add_char_descr.attr_handle;
        esp_err_t ar = esp_ble_gap_config_adv_data(&s_adv_data);
        if (ar != ESP_OK) {
            ESP_LOGE(TAG, "config_adv_data failed: %s", esp_err_to_name(ar));
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_prof.conn_id = param->connect.conn_id;
        s_prof.connected = true;
        ESP_LOGI(TAG, "connected conn_id=%u", (unsigned)s_prof.conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_prof.connected = false;
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_GATTS_READ_EVT:
        if (param->read.need_rsp) {
            if (param->read.handle == s_prof.tx_handle) {
                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(rsp));
                rsp.attr_value.handle = param->read.handle;
                rsp.attr_value.len = 0;
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            } else {
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_READ_NOT_PERMIT,
                                            NULL);
            }
        }
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_prof.tx_cccd_handle) {
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;
        }
        if (param->write.is_prep) {
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_NOT_LONG,
                                            NULL);
            }
            break;
        }
        if (param->write.handle == s_prof.rx_handle && param->write.len > 0) {
            char buf[384];
            size_t n = param->write.len;
            if (n >= sizeof(buf)) {
                n = sizeof(buf) - 1;
            }
            memcpy(buf, param->write.value, n);
            buf[n] = '\0';
            handle_cmd(buf);
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;

    case ESP_GATTS_MTU_EVT:
        s_mtu = param->mtu.mtu;
        break;

    default:
        break;
    }
}

esp_err_t ble_settings_start(void)
{
#if !CONFIG_BT_ENABLED
    ESP_LOGW(TAG, "CONFIG_BT_ENABLED is off");
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (s_started) {
        return ESP_OK;
    }

    memset(&s_prof, 0, sizeof(s_prof));

    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "controller_mem_release: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller_init: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller_enable: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid_init: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid_enable: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_ble_gatts_app_register(BLE_PROFILE_APP_ID);
    if (ret != ESP_OK) {
        return ret;
    }

    (void)esp_ble_gatt_set_local_mtu(512);

    s_started = true;
    ESP_LOGI(TAG, "BLE settings GATT registered (service 0x%04x)", SVC_UUID);
    return ESP_OK;
}
