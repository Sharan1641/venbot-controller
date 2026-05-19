#include "vending_machine.h"
#include "servo.h"
#include "ir_sensor.h"
#include "oled.h"
#include "led.h"
#include "ble_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "VM";

// ─── Globals ──────────────────────────────────────────────────────────────────

QueueHandle_t     g_vm_event_queue;
SemaphoreHandle_t g_vm_mutex;
vm_status_t       g_vm_status;
vm_slot_t         g_vm_slots[VM_MAX_SLOTS];

// Admin PIN (matches BLE server)
static const uint8_t ADMIN_PIN[4] = {0x01, 0x02, 0x03, 0x04};

#define IR_TIMEOUT_MS   3000  // Wait up to 3s for IR confirmation

// ─── Default inventory ────────────────────────────────────────────────────────

static void vm_load_defaults(void) {
    const char *names[] = {"Cola", "Water", "Chips", "Candy", "Cookie", "Juice"};
    const uint16_t prices[] = {150, 100, 200, 125, 175, 175}; // cents
    for (int i = 0; i < VM_MAX_SLOTS; i++) {
        g_vm_slots[i].slot        = i;
        g_vm_slots[i].count       = VM_SLOT_CAPACITY;
        g_vm_slots[i].price_cents = prices[i];
        strncpy(g_vm_slots[i].name, names[i], sizeof(g_vm_slots[i].name));
    }
}

// ─── State transitions ────────────────────────────────────────────────────────

static void vm_set_state(vm_state_t new_state) {
    xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
    g_vm_status.state = new_state;
    xSemaphoreGive(g_vm_mutex);
    ble_notify_status(&g_vm_status);
}

static void vm_set_error(vm_error_t err) {
    xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
    g_vm_status.state = VM_STATE_ERROR;
    g_vm_status.error = err;
    xSemaphoreGive(g_vm_mutex);
    ble_notify_status(&g_vm_status);
    oled_show_error(err);
    led_set_status(LED_STATUS_ERROR);
}

// ─── Dispense logic ───────────────────────────────────────────────────────────

static void vm_do_dispense(uint8_t slot) {
    ESP_LOGI(TAG, "Dispensing slot %d (%s)", slot, g_vm_slots[slot].name);

    vm_set_state(VM_STATE_DISPENSING);
    oled_show_dispensing(slot);
    led_set_status(LED_STATUS_DISPENSING);

    // Activate servo
    servo_dispense(slot, 800);

    // Wait for IR confirmation
    bool item_detected = false;
    uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(IR_TIMEOUT_MS);
    while (xTaskGetTickCount() < deadline) {
        if (ir_sensor_read(slot)) {
            item_detected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!item_detected) {
        ESP_LOGW(TAG, "IR timeout — possible jam on slot %d", slot);
        vm_set_error(VM_ERR_JAM);
        return;
    }

    // Decrement inventory
    xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
    if (g_vm_slots[slot].count > 0) g_vm_slots[slot].count--;
    g_vm_status.total_dispensed++;
    g_vm_status.selected_slot = 0xFF;
    xSemaphoreGive(g_vm_mutex);

    vm_set_state(VM_STATE_DISPENSED);
    oled_show_success(slot);
    led_set_status(LED_STATUS_SUCCESS);
    ble_notify_inventory(g_vm_slots, VM_MAX_SLOTS);

    vTaskDelay(pdMS_TO_TICKS(2000));

    vm_set_state(VM_STATE_IDLE);
    oled_show_idle();
    led_set_status(LED_STATUS_IDLE);

    ESP_LOGI(TAG, "Slot %d dispensed OK. Remaining: %d", slot, g_vm_slots[slot].count);
}

// ─── Admin command handler ────────────────────────────────────────────────────

static void vm_handle_admin(const admin_cmd_pkt_t *cmd) {
    if (!g_vm_status.admin_unlocked) {
        ESP_LOGW(TAG, "Admin command rejected: not authenticated");
        vm_set_error(VM_ERR_AUTH);
        return;
    }

    switch (cmd->cmd) {
        case ADMIN_CMD_RESTOCK:
            if (cmd->slot < VM_MAX_SLOTS) {
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                g_vm_slots[cmd->slot].count = cmd->count;
                xSemaphoreGive(g_vm_mutex);
                ble_notify_inventory(g_vm_slots, VM_MAX_SLOTS);
                oled_show_inventory(g_vm_slots, VM_MAX_SLOTS);
                ESP_LOGI(TAG, "Slot %d restocked to %d", cmd->slot, cmd->count);
            }
            break;

        case ADMIN_CMD_SET_PRICE:
            if (cmd->slot < VM_MAX_SLOTS) {
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                g_vm_slots[cmd->slot].price_cents = cmd->price_cents;
                xSemaphoreGive(g_vm_mutex);
                ESP_LOGI(TAG, "Slot %d price → $%d.%02d",
                         cmd->slot, cmd->price_cents / 100, cmd->price_cents % 100);
            }
            break;

        case ADMIN_CMD_RESET_ERR:
            xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
            g_vm_status.error = VM_ERR_NONE;
            xSemaphoreGive(g_vm_mutex);
            vm_set_state(VM_STATE_IDLE);
            oled_show_idle();
            led_set_status(LED_STATUS_IDLE);
            break;

        case ADMIN_CMD_LOCK:
            xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
            g_vm_status.locked = true;
            xSemaphoreGive(g_vm_mutex);
            ESP_LOGI(TAG, "Machine LOCKED");
            break;

        case ADMIN_CMD_UNLOCK:
            xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
            g_vm_status.locked = false;
            xSemaphoreGive(g_vm_mutex);
            ESP_LOGI(TAG, "Machine UNLOCKED");
            break;
    }
}

// ─── Main task ────────────────────────────────────────────────────────────────

void vending_machine_task(void *arg) {
    vm_event_t evt;

    oled_show_idle();
    led_set_status(LED_STATUS_IDLE);

    while (1) {
        if (xQueueReceive(g_vm_event_queue, &evt, portMAX_DELAY) != pdTRUE) continue;

        switch (evt.type) {

            case EVT_SELECT_SLOT:
                if (g_vm_status.locked) break;
                if (g_vm_status.state == VM_STATE_ERROR) break;
                if (evt.slot >= VM_MAX_SLOTS) {
                    vm_set_error(VM_ERR_INVALID_SLOT);
                    break;
                }
                if (g_vm_slots[evt.slot].count == 0) {
                    vm_set_error(VM_ERR_EMPTY);
                    break;
                }
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                g_vm_status.selected_slot = evt.slot;
                xSemaphoreGive(g_vm_mutex);
                vm_set_state(VM_STATE_SELECTED);
                oled_show_selected(evt.slot,
                                   g_vm_slots[evt.slot].name,
                                   g_vm_slots[evt.slot].price_cents);
                led_set_status(LED_STATUS_SELECTED);
                ESP_LOGI(TAG, "Slot %d selected", evt.slot);
                break;

            case EVT_DISPENSE:
                if (g_vm_status.locked) break;
                if (g_vm_status.state != VM_STATE_SELECTED) break;
                vm_do_dispense(g_vm_status.selected_slot);
                break;

            case EVT_AUTH: {
                bool ok = (memcmp(evt.pin, ADMIN_PIN, 4) == 0);
                xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                g_vm_status.admin_unlocked = ok;
                xSemaphoreGive(g_vm_mutex);
                if (ok) {
                    vm_set_state(VM_STATE_ADMIN);
                    oled_show_admin();
                    led_set_status(LED_STATUS_ADMIN);
                    ESP_LOGI(TAG, "Admin authenticated");
                } else {
                    vm_set_error(VM_ERR_AUTH);
                    ESP_LOGW(TAG, "Admin auth failed");
                }
                break;
            }

            case EVT_ADMIN_CMD:
                vm_handle_admin(&evt.admin);
                break;

            case EVT_IR_TRIGGER:
                // Interrupt-driven — only meaningful during dispensing
                ESP_LOGD(TAG, "IR trigger: slot %d", evt.ir_slot);
                break;

            case EVT_RESET_ERROR:
                if (g_vm_status.state == VM_STATE_ERROR) {
                    xSemaphoreTake(g_vm_mutex, portMAX_DELAY);
                    g_vm_status.error = VM_ERR_NONE;
                    xSemaphoreGive(g_vm_mutex);
                    vm_set_state(VM_STATE_IDLE);
                    oled_show_idle();
                    led_set_status(LED_STATUS_IDLE);
                }
                break;
        }
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────

void vending_machine_init(void) {
    g_vm_event_queue = xQueueCreate(16, sizeof(vm_event_t));
    g_vm_mutex       = xSemaphoreCreateMutex();

    memset(&g_vm_status, 0, sizeof(g_vm_status));
    g_vm_status.state         = VM_STATE_IDLE;
    g_vm_status.selected_slot = 0xFF;

    vm_load_defaults();
}

void vm_notify_ble_status(void) {
    ble_notify_status(&g_vm_status);
}

void vm_notify_ble_inventory(void) {
    ble_notify_inventory(g_vm_slots, VM_MAX_SLOTS);
}
