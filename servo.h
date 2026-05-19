#pragma once
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize all servo channels via LEDC PWM.
 *        Each slot maps to one servo on GPIO_SERVO_BASE + slot_index.
 */
esp_err_t servo_init(void);

/**
 * @brief Rotate servo to specified angle (0–180°).
 */
esp_err_t servo_set_angle(uint8_t slot, uint8_t angle_deg);

/**
 * @brief Perform a full dispense cycle: rotate to SERVO_DISPENSE_ANGLE,
 *        hold for hold_ms, then return to rest.
 */
esp_err_t servo_dispense(uint8_t slot, uint32_t hold_ms);
