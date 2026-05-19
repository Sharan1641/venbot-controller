#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// ─── Configuration ────────────────────────────────────────────────────────────

#define VM_MAX_SLOTS        6       // Number of vending slots
#define VM_SLOT_CAPACITY    5       // Max items per slot

// GPIO Assignments
#define GPIO_SERVO_BASE     18      // Servo 0 = GPIO18, 1 = GPIO19 ... 5 = GPIO23
#define GPIO_IR_SENSOR_BASE 25      // IR 0 = GPIO25, 1 = GPIO26 ... 5 = GPIO30
#define GPIO_LED_STATUS     2       // Onboard LED / status
#define GPIO_LED_DISPENSE   4       // Green dispense LED
#define GPIO_LED_ERROR      5       // Red error LED

// I2C for OLED (SSD1306 128x64)
#define I2C_MASTER_SCL      22
#define I2C_MASTER_SDA      21
#define I2C_MASTER_FREQ_HZ  400000
#define OLED_I2C_ADDR       0x3C

// Servo PWM
#define SERVO_MIN_PULSE_US  500     // Microseconds (0°)
#define SERVO_MAX_PULSE_US  2400    // Microseconds (180°)
#define SERVO_FREQ_HZ       50
#define SERVO_DISPENSE_ANGLE 180    // Degrees to rotate for dispense
#define SERVO_REST_ANGLE    0

// BLE
#define BLE_DEVICE_NAME     "VendBot-ESP32"

// ─── BLE GATT UUIDs ──────────────────────────────────────────────────────────
// Service: Vending Machine Primary Service
#define VM_SERVICE_UUID             0xAB00
// Characteristics
#define VM_CHAR_SELECT_UUID         0xAB01  // W   : uint8  - slot index (0-5)
#define VM_CHAR_DISPENSE_UUID       0xAB02  // W   : uint8  - trigger dispense (write 0x01)
#define VM_CHAR_STATUS_UUID         0xAB03  // R/N : VM status struct
#define VM_CHAR_INVENTORY_UUID      0xAB04  // R/N : inventory array [6 bytes]
#define VM_CHAR_AUTH_UUID           0xAB05  // W   : 4-byte PIN for admin
#define VM_CHAR_ADMIN_CMD_UUID      0xAB06  // W   : admin command struct
#define VM_CHAR_PRICE_UUID          0xAB07  // R/W : price table [6 x uint16 cents]

// ─── Types ────────────────────────────────────────────────────────────────────

typedef enum {
    VM_STATE_IDLE,
    VM_STATE_SELECTED,
    VM_STATE_DISPENSING,
    VM_STATE_DISPENSED,
    VM_STATE_ERROR,
    VM_STATE_ADMIN,
} vm_state_t;

typedef enum {
    VM_ERR_NONE         = 0x00,
    VM_ERR_EMPTY        = 0x01,  // Slot empty
    VM_ERR_JAM          = 0x02,  // IR didn't detect item drop
    VM_ERR_AUTH         = 0x03,  // Bad admin PIN
    VM_ERR_INVALID_SLOT = 0x04,
} vm_error_t;

typedef enum {
    ADMIN_CMD_RESTOCK   = 0x01,  // payload: slot, count
    ADMIN_CMD_SET_PRICE = 0x02,  // payload: slot, price_cents (uint16)
    ADMIN_CMD_RESET_ERR = 0x03,
    ADMIN_CMD_LOCK      = 0x04,
    ADMIN_CMD_UNLOCK    = 0x05,
} admin_cmd_t;

typedef struct {
    uint8_t     slot;
    uint8_t     count;
    uint16_t    price_cents;
    char        name[12];
} vm_slot_t;

typedef struct {
    vm_state_t  state;
    vm_error_t  error;
    uint8_t     selected_slot;
    bool        admin_unlocked;
    bool        locked;         // Machine locked (out of service)
    uint32_t    total_dispensed;
} vm_status_t;

typedef struct {
    admin_cmd_t cmd;
    uint8_t     slot;
    uint8_t     count;
    uint16_t    price_cents;
} admin_cmd_pkt_t;

// ─── Events (inter-task) ──────────────────────────────────────────────────────

typedef enum {
    EVT_SELECT_SLOT,
    EVT_DISPENSE,
    EVT_AUTH,
    EVT_ADMIN_CMD,
    EVT_IR_TRIGGER,
    EVT_RESET_ERROR,
} vm_event_type_t;

typedef struct {
    vm_event_type_t type;
    union {
        uint8_t         slot;
        admin_cmd_pkt_t admin;
        uint8_t         pin[4];
        uint8_t         ir_slot;
    };
} vm_event_t;

// ─── Global handles ───────────────────────────────────────────────────────────

extern QueueHandle_t    g_vm_event_queue;
extern SemaphoreHandle_t g_vm_mutex;
extern vm_status_t      g_vm_status;
extern vm_slot_t        g_vm_slots[VM_MAX_SLOTS];

// ─── Function declarations ────────────────────────────────────────────────────

void vending_machine_init(void);
void vending_machine_task(void *arg);
void vm_notify_ble_status(void);
void vm_notify_ble_inventory(void);
