#include "servo.h"
#include "vending_machine.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SERVO";

// LEDC timer shared across all servo channels
#define SERVO_LEDC_TIMER    LEDC_TIMER_0
#define SERVO_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_DUTY_RES LEDC_TIMER_14_BIT
#define SERVO_LEDC_MAX_DUTY ((1 << 14) - 1)

static uint32_t angle_to_duty(uint8_t angle) {
    // Map 0–180° → SERVO_MIN_PULSE_US – SERVO_MAX_PULSE_US
    uint32_t pulse_us = SERVO_MIN_PULSE_US +
        ((uint32_t)(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle) / 180;
    // Convert pulse_us to duty cycle for 50 Hz (period = 20000 us)
    return (pulse_us * SERVO_LEDC_MAX_DUTY) / 20000;
}

esp_err_t servo_init(void) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = SERVO_LEDC_MODE,
        .timer_num       = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_LEDC_DUTY_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    for (int i = 0; i < VM_MAX_SLOTS; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num   = GPIO_SERVO_BASE + i,
            .speed_mode = SERVO_LEDC_MODE,
            .channel    = (ledc_channel_t)i,
            .timer_sel  = SERVO_LEDC_TIMER,
            .duty       = angle_to_duty(SERVO_REST_ANGLE),
            .hpoint     = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
        ESP_LOGI(TAG, "Servo %d → GPIO %d init OK", i, GPIO_SERVO_BASE + i);
    }
    return ESP_OK;
}

esp_err_t servo_set_angle(uint8_t slot, uint8_t angle_deg) {
    if (slot >= VM_MAX_SLOTS) return ESP_ERR_INVALID_ARG;
    uint32_t duty = angle_to_duty(angle_deg);
    ledc_set_duty(SERVO_LEDC_MODE, (ledc_channel_t)slot, duty);
    ledc_update_duty(SERVO_LEDC_MODE, (ledc_channel_t)slot);
    return ESP_OK;
}

esp_err_t servo_dispense(uint8_t slot, uint32_t hold_ms) {
    if (slot >= VM_MAX_SLOTS) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Slot %d: dispensing...", slot);

    // Rotate to dispense position
    servo_set_angle(slot, SERVO_DISPENSE_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));

    // Return to rest
    servo_set_angle(slot, SERVO_REST_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(300)); // settle
    return ESP_OK;
}
