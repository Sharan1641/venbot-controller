#pragma once
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize IR sensor GPIO inputs with interrupts.
 *        When an item falls through the chute, the IR beam is broken,
 *        triggering an interrupt that posts EVT_IR_TRIGGER to g_vm_event_queue.
 */
esp_err_t ir_sensor_init(void);

/**
 * @brief Poll IR sensor for a specific slot synchronously.
 * @return true if beam is broken (item detected), false otherwise.
 */
bool ir_sensor_read(uint8_t slot);
