#include "ir_sensor.h"
#include "vending_machine.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "IR";

static void IRAM_ATTR ir_isr_handler(void *arg) {
    uint8_t slot = (uint8_t)(uintptr_t)arg;
    vm_event_t evt = {
        .type    = EVT_IR_TRIGGER,
        .ir_slot = slot,
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(g_vm_event_queue, &evt, &woken);
    if (woken) portYIELD_FROM_ISR();
}

esp_err_t ir_sensor_init(void) {
    gpio_config_t cfg = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,  // Active-low IR receivers
        .pin_bit_mask = 0,
    };

    for (int i = 0; i < VM_MAX_SLOTS; i++) {
        cfg.pin_bit_mask |= (1ULL << (GPIO_IR_SENSOR_BASE + i));
    }
    ESP_ERROR_CHECK(gpio_config(&cfg));

    gpio_install_isr_service(0);

    for (int i = 0; i < VM_MAX_SLOTS; i++) {
        gpio_isr_handler_add(GPIO_IR_SENSOR_BASE + i, ir_isr_handler,
                             (void *)(uintptr_t)i);
        ESP_LOGI(TAG, "IR sensor %d → GPIO %d init OK", i, GPIO_IR_SENSOR_BASE + i);
    }
    return ESP_OK;
}

bool ir_sensor_read(uint8_t slot) {
    if (slot >= VM_MAX_SLOTS) return false;
    // Active-low: LOW = beam broken = item detected
    return gpio_get_level(GPIO_IR_SENSOR_BASE + slot) == 0;
}
