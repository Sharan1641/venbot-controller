#pragma once
#include "esp_err.h"
#include "vending_machine.h"

/**
 * @brief Initialize NVS, BT controller, Bluedroid stack, and register
 *        the vending machine GATT server with all characteristics.
 */
esp_err_t ble_server_init(void);

/**
 * @brief Notify connected BLE client of updated machine status.
 *        Called from vending_machine_task after state changes.
 */
void ble_notify_status(const vm_status_t *status);

/**
 * @brief Notify connected BLE client of updated inventory.
 */
void ble_notify_inventory(const vm_slot_t slots[], uint8_t count);
