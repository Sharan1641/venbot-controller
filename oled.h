#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "vending_machine.h"

esp_err_t oled_init(void);
void oled_show_idle(void);
void oled_show_selected(uint8_t slot, const char *name, uint16_t price_cents);
void oled_show_dispensing(uint8_t slot);
void oled_show_success(uint8_t slot);
void oled_show_error(vm_error_t err);
void oled_show_admin(void);
void oled_show_inventory(vm_slot_t slots[], uint8_t count);
