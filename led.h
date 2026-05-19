#pragma once
#include "esp_err.h"

typedef enum {
    LED_STATUS_IDLE,
    LED_STATUS_SELECTED,
    LED_STATUS_DISPENSING,
    LED_STATUS_SUCCESS,
    LED_STATUS_ERROR,
    LED_STATUS_ADMIN,
} led_status_t;

esp_err_t led_init(void);
void led_set_status(led_status_t status);
void led_blink(int gpio, int count, int on_ms, int off_ms);
