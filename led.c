#include "led.h"
#include "vending_machine.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t led_init(void) {
    gpio_config_t cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << GPIO_LED_STATUS) |
                        (1ULL << GPIO_LED_DISPENSE) |
                        (1ULL << GPIO_LED_ERROR),
    };
    gpio_config(&cfg);
    gpio_set_level(GPIO_LED_STATUS,   0);
    gpio_set_level(GPIO_LED_DISPENSE, 0);
    gpio_set_level(GPIO_LED_ERROR,    0);
    return ESP_OK;
}

void led_blink(int gpio, int count, int on_ms, int off_ms) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        gpio_set_level(gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

void led_set_status(led_status_t status) {
    // Clear all
    gpio_set_level(GPIO_LED_STATUS,   0);
    gpio_set_level(GPIO_LED_DISPENSE, 0);
    gpio_set_level(GPIO_LED_ERROR,    0);

    switch (status) {
        case LED_STATUS_IDLE:
            gpio_set_level(GPIO_LED_STATUS, 1);
            break;
        case LED_STATUS_SELECTED:
            led_blink(GPIO_LED_DISPENSE, 2, 150, 100);
            gpio_set_level(GPIO_LED_DISPENSE, 1);
            break;
        case LED_STATUS_DISPENSING:
            gpio_set_level(GPIO_LED_DISPENSE, 1);
            break;
        case LED_STATUS_SUCCESS:
            led_blink(GPIO_LED_DISPENSE, 3, 200, 100);
            break;
        case LED_STATUS_ERROR:
            gpio_set_level(GPIO_LED_ERROR, 1);
            led_blink(GPIO_LED_ERROR, 5, 100, 100);
            break;
        case LED_STATUS_ADMIN:
            led_blink(GPIO_LED_STATUS, 3, 100, 100);
            gpio_set_level(GPIO_LED_STATUS, 1);
            break;
    }
}
