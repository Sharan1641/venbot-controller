#include "vending_machine.h"
#include "ble_server.h"
#include "servo.h"
#include "ir_sensor.h"
#include "oled.h"
#include "led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "=== VendBot ESP32 Vending Machine ===");
    ESP_LOGI(TAG, "Initializing subsystems...");

    // Hardware init
    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(oled_init());
    ESP_ERROR_CHECK(servo_init());
    ESP_ERROR_CHECK(ir_sensor_init());

    // State machine + event queue
    vending_machine_init();

    // BLE GATT server
    ESP_ERROR_CHECK(ble_server_init());

    ESP_LOGI(TAG, "All systems GO. Starting VM task.");

    // Launch main vending machine task
    xTaskCreate(vending_machine_task, "vm_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "VendBot ready. BLE advertising as \"%s\"", BLE_DEVICE_NAME);
}
