#include "ble_server.h"
#include "vending_machine.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "BLE";

// ─── GATT profile / handles ───────────────────────────────────────────────────

#define GATTS_APP_ID        0
#define GATTS_NUM_HANDLES   20  // service + 7 chars × 2 (value + CCC) + service

// Attribute index
enum {
    IDX_SVC,
    IDX_CHAR_SELECT,        IDX_CHAR_SELECT_VAL,
    IDX_CHAR_DISPENSE,      IDX_CHAR_DISPENSE_VAL,
    IDX_CHAR_STATUS,        IDX_CHAR_STATUS_VAL,    IDX_CHAR_STATUS_CFG,
    IDX_CHAR_INVENTORY,     IDX_CHAR_INVENTORY_VAL, IDX_CHAR_INVENTORY_CFG,
    IDX_CHAR_AUTH,          IDX_CHAR_AUTH_VAL,
    IDX_CHAR_ADMIN,         IDX_CHAR_ADMIN_VAL,
    IDX_CHAR_PRICE,         IDX_CHAR_PRICE_VAL,
    IDX_NB,
};

static uint16_t s_handle_table[IDX_NB];
static uint16_t s_conn_id       = 0xFFFF;
static uint16_t s_gatts_if      = ESP_GATT_IF_NONE;
static bool     s_notify_status = false;
static bool     s_notify_inv    = false;

// Admin PIN (default 1234)
static uint8_t s_admin_pin[4] = {0x01, 0x02, 0x03, 0x04};

// ─── UUIDs ────────────────────────────────────────────────────────────────────

static const uint16_t SVC_UUID        = VM_SERVICE_UUID;
static const uint16_t CHAR_SELECT_UUID   = VM_CHAR_SELECT_UUID;
static const uint16_t CHAR_DISPENSE_UUID = VM_CHAR_DISPENSE_UUID;
static const uint16_t CHAR_STATUS_UUID   = VM_CHAR_STATUS_UUID;
static const uint16_t CHAR_INV_UUID      = VM_CHAR_INVENTORY_UUID;
static const uint16_t CHAR_AUTH_UUID     = VM_CHAR_AUTH_UUID;
static const uint16_t CHAR_ADMIN_UUID    = VM_CHAR_ADMIN_CMD_UUID;
static const uint16_t CHAR_PRICE_UUID    = VM_CHAR_PRICE_UUID;
static const uint16_t primary_service_uuid   = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_decl_uuid        = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t char_client_cfg_uuid  = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_rw  = ESP_GATT_CHAR_PROP_BIT_READ  | ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_w   = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_rn  = ESP_GATT_CHAR_PROP_BIT_READ  | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_rwn = ESP_GATT_CHAR_PROP_BIT_READ  | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static const uint8_t ccc_val[2] = {0x00, 0x00};

static const esp_gatts_attr_db_t gatt_db[IDX_NB] = {
    // Service
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16,(uint8_t*)&primary_service_uuid,
        ESP_GATT_PERM_READ, sizeof(SVC_UUID), sizeof(SVC_UUID), (uint8_t*)&SVC_UUID}},

    // Slot Select (W)
    [IDX_CHAR_SELECT]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_w}},
    [IDX_CHAR_SELECT_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_SELECT_UUID,
        ESP_GATT_PERM_WRITE, 1, 1, NULL}},

    // Dispense Trigger (W)
    [IDX_CHAR_DISPENSE]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_w}},
    [IDX_CHAR_DISPENSE_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_DISPENSE_UUID,
        ESP_GATT_PERM_WRITE, 1, 1, NULL}},

    // Status (R/Notify)
    [IDX_CHAR_STATUS]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_rn}},
    [IDX_CHAR_STATUS_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_STATUS_UUID,
        ESP_GATT_PERM_READ, sizeof(vm_status_t), 0, NULL}},
    [IDX_CHAR_STATUS_CFG] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_client_cfg_uuid,
        ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE, 2, 2, (uint8_t*)ccc_val}},

    // Inventory (R/Notify)
    [IDX_CHAR_INVENTORY]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_rn}},
    [IDX_CHAR_INVENTORY_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_INV_UUID,
        ESP_GATT_PERM_READ, VM_MAX_SLOTS, 0, NULL}},
    [IDX_CHAR_INVENTORY_CFG] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_client_cfg_uuid,
        ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE, 2, 2, (uint8_t*)ccc_val}},

    // Admin Auth (W)
    [IDX_CHAR_AUTH]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_w}},
    [IDX_CHAR_AUTH_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_AUTH_UUID,
        ESP_GATT_PERM_WRITE, 4, 4, NULL}},

    // Admin Command (W)
    [IDX_CHAR_ADMIN]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_w}},
    [IDX_CHAR_ADMIN_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_ADMIN_UUID,
        ESP_GATT_PERM_WRITE, sizeof(admin_cmd_pkt_t), 0, NULL}},

    // Price Table (R/W)
    [IDX_CHAR_PRICE]     = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&char_decl_uuid,
        ESP_GATT_PERM_READ, 1, 1, (uint8_t*)&char_prop_rw}},
    [IDX_CHAR_PRICE_VAL] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,(uint8_t*)&CHAR_PRICE_UUID,
        ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
        VM_MAX_SLOTS * sizeof(uint16_t), 0, NULL}},
};

// ─── GAP advertising ──────────────────────────────────────────────────────────

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        esp_ble_gap_start_advertising(&s_adv_params);
    }
}

// ─── GATT write handler ───────────────────────────────────────────────────────

static void handle_write(uint16_t handle, uint8_t *data, uint16_t len) {
    vm_event_t evt = {0};

    if (handle == s_handle_table[IDX_CHAR_SELECT_VAL]) {
        evt.type = EVT_SELECT_SLOT;
        evt.slot = data[0];
        xQueueSend(g_vm_event_queue, &evt, 0);

    } else if (handle == s_handle_table[IDX_CHAR_DISPENSE_VAL]) {
        if (data[0] == 0x01) {
            evt.type = EVT_DISPENSE;
            xQueueSend(g_vm_event_queue, &evt, 0);
        }

    } else if (handle == s_handle_table[IDX_CHAR_AUTH_VAL]) {
        if (len == 4) {
            evt.type = EVT_AUTH;
            memcpy(evt.pin, data, 4);
            xQueueSend(g_vm_event_queue, &evt, 0);
        }

    } else if (handle == s_handle_table[IDX_CHAR_ADMIN_VAL]) {
        if (len >= sizeof(admin_cmd_pkt_t)) {
            evt.type = EVT_ADMIN_CMD;
            memcpy(&evt.admin, data, sizeof(admin_cmd_pkt_t));
            xQueueSend(g_vm_event_queue, &evt, 0);
        }

    } else if (handle == s_handle_table[IDX_CHAR_PRICE_VAL]) {
        // Update price table directly (admin only enforced in VM task)
        uint16_t *prices = (uint16_t *)data;
        uint8_t n = len / 2;
        xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
        for (int i = 0; i < n && i < VM_MAX_SLOTS; i++) {
            g_vm_slots[i].price_cents = prices[i];
        }
        xSemaphoreGive(g_vm_mutex);
    }
}

// ─── GATTS event handler ──────────────────────────────────────────────────────

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            s_gatts_if = gatts_if;
            esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
            esp_ble_gap_config_adv_data_raw(
                (uint8_t[]){
                    0x02, 0x01, 0x06,                         // Flags
                    0x03, 0x03, 0x00, 0xAB,                   // Service UUID
                    0x0F, 0x09, 'V','e','n','d','B','o','t',  // Complete name
                    '-','E','S','P','3','2'
                }, 21);
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_NB, 0);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            memcpy(s_handle_table, param->add_attr_tab.handles,
                   IDX_NB * sizeof(uint16_t));
            esp_ble_gatts_start_service(s_handle_table[IDX_SVC]);
            ESP_LOGI(TAG, "GATT service started, handles allocated");
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_conn_id = param->connect.conn_id;
            ESP_LOGI(TAG, "BLE client connected, conn_id=%d", s_conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_conn_id = 0xFFFF;
            s_notify_status = false;
            s_notify_inv    = false;
            ESP_LOGI(TAG, "BLE client disconnected");
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            handle_write(param->write.handle,
                         param->write.value, param->write.len);
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if,
                    param->write.conn_id, param->write.trans_id,
                    ESP_GATT_OK, NULL);
            }
            break;

        case ESP_GATTS_READ_EVT: {
            esp_gatt_rsp_t rsp = {0};
            if (param->read.handle == s_handle_table[IDX_CHAR_STATUS_VAL]) {
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                memcpy(rsp.attr_value.value, &g_vm_status, sizeof(vm_status_t));
                rsp.attr_value.len = sizeof(vm_status_t);
                xSemaphoreGive(g_vm_mutex);
            } else if (param->read.handle == s_handle_table[IDX_CHAR_INVENTORY_VAL]) {
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                for (int i = 0; i < VM_MAX_SLOTS; i++)
                    rsp.attr_value.value[i] = g_vm_slots[i].count;
                rsp.attr_value.len = VM_MAX_SLOTS;
                xSemaphoreGive(g_vm_mutex);
            } else if (param->read.handle == s_handle_table[IDX_CHAR_PRICE_VAL]) {
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                uint16_t *p = (uint16_t *)rsp.attr_value.value;
                for (int i = 0; i < VM_MAX_SLOTS; i++) p[i] = g_vm_slots[i].price_cents;
                rsp.attr_value.len = VM_MAX_SLOTS * 2;
                xSemaphoreGive(g_vm_mutex);
            }
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                param->read.trans_id, ESP_GATT_OK, &rsp);
            break;
        }

        default:
            break;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t ble_server_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(512));

    ESP_LOGI(TAG, "BLE GATT server initialized as \"%s\"", BLE_DEVICE_NAME);
    return ESP_OK;
}

void ble_notify_status(const vm_status_t *status) {
    if (s_conn_id == 0xFFFF || s_gatts_if == ESP_GATT_IF_NONE) return;
    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
        s_handle_table[IDX_CHAR_STATUS_VAL],
        sizeof(vm_status_t), (uint8_t *)status, false);
}

void ble_notify_inventory(const vm_slot_t slots[], uint8_t count) {
    if (s_conn_id == 0xFFFF || s_gatts_if == ESP_GATT_IF_NONE) return;
    uint8_t buf[VM_MAX_SLOTS];
    for (int i = 0; i < count; i++) buf[i] = slots[i].count;
    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
        s_handle_table[IDX_CHAR_INVENTORY_VAL],
        count, buf, false);
}
