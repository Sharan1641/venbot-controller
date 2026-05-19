# VendBot ESP32 — Vending Machine Firmware

ESP-IDF based BLE-controlled vending machine with 6 servo-driven slots,
OLED status display, IR drop detection, and LED indicators.

---

## Hardware

| Component        | Part                     | Count |
|-----------------|--------------------------|-------|
| MCU              | ESP32 DevKit v1          | 1     |
| Servo motors     | SG90 / MG996R (coil)     | 6     |
| IR sensors       | TCRT5000 or HY-301       | 6     |
| OLED display     | SSD1306 128×64 I2C       | 1     |
| Status LED       | Green 5mm                | 1     |
| Dispense LED     | Green 5mm                | 1     |
| Error LED        | Red 5mm                  | 1     |
| Resistors        | 220Ω                     | 3     |
| Power supply     | 5V 3A minimum            | 1     |

---

## Wiring

### Servos (PWM via LEDC)
```
Servo 0 → GPIO 18    Servo 3 → GPIO 21 (NOTE: share with SDA if conflict, remap)
Servo 1 → GPIO 19    Servo 4 → GPIO 22 (NOTE: share with SCL if conflict, remap)
Servo 2 → GPIO 20    Servo 5 → GPIO 23
```
> ⚠️  GPIO 21/22 are used for I2C (SDA/SCL). If you use all 6 servos,
> change GPIO_SERVO_BASE to 12 in vending_machine.h and remap I2C to 26/27.

**Recommended safe GPIO for 6 servos:** 12, 13, 14, 15, 32, 33

### IR Sensors (active-low, pull-up enabled)
```
IR 0 → GPIO 25    IR 3 → GPIO 28
IR 1 → GPIO 26    IR 4 → GPIO 29
IR 2 → GPIO 27    IR 5 → GPIO 30
```
Connect VCC → 3.3V, GND → GND, OUT → GPIO

### OLED Display (I2C)
```
SDA → GPIO 21
SCL → GPIO 22
VCC → 3.3V
GND → GND
```

### LEDs
```
Status (green)   → GPIO 2  → 220Ω → GND
Dispense (green) → GPIO 4  → 220Ω → GND
Error (red)      → GPIO 5  → 220Ω → GND
```

---

## Project Structure

```
esp32-vending-machine/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── main.c               ← app_main, subsystem init
│   ├── vending_machine.h    ← all types, UUIDs, globals
│   └── vending_machine.c    ← state machine, event loop
└── components/
    ├── ble/
    │   ├── ble_server.h
    │   └── ble_server.c     ← GATT server, all characteristics
    ├── servo/
    │   ├── servo.h
    │   └── servo.c          ← LEDC PWM servo control
    ├── ir_sensor/
    │   ├── ir_sensor.h
    │   └── ir_sensor.c      ← GPIO interrupt-driven IR
    ├── oled/
    │   ├── oled.h
    │   └── oled.c           ← SSD1306 I2C driver + screens
    └── led/
        ├── led.h
        └── led.c            ← Status / dispense / error LEDs
```

---

## Build & Flash

```bash
# Set up ESP-IDF environment (v5.x recommended)
. $IDF_PATH/export.sh

# Configure
idf.py menuconfig   # verify BT + LEDC settings

# Build
idf.py build

# Flash + monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## BLE GATT Profile

**Service UUID:** `0xAB00`

| Characteristic | UUID    | Props    | Description                        |
|---------------|---------|----------|------------------------------------|
| SELECT         | 0xAB01  | Write    | Write slot index (0–5)             |
| DISPENSE       | 0xAB02  | Write    | Write 0x01 to trigger dispense     |
| STATUS         | 0xAB03  | R/Notify | vm_status_t struct (10 bytes)      |
| INVENTORY      | 0xAB04  | R/Notify | 6-byte array of item counts        |
| AUTH           | 0xAB05  | Write    | Write 4-byte admin PIN             |
| ADMIN_CMD      | 0xAB06  | Write    | admin_cmd_pkt_t struct             |
| PRICE          | 0xAB07  | R/W      | 6× uint16 price table (cents)      |

**Admin PIN default:** `{0x01, 0x02, 0x03, 0x04}` → decimal `1234`

### Admin Commands (ADMIN_CMD characteristic)
| Byte 0 | Command      | Byte 1 | Byte 2 | Bytes 3-4   |
|--------|-------------|--------|--------|-------------|
| 0x01   | RESTOCK      | slot   | count  | —           |
| 0x02   | SET_PRICE    | slot   | —      | price_cents |
| 0x03   | RESET_ERROR  | —      | —      | —           |
| 0x04   | LOCK         | —      | —      | —           |
| 0x05   | UNLOCK       | —      | —      | —           |

---

## State Machine

```
IDLE ──(select)──▶ SELECTED ──(dispense)──▶ DISPENSING
  ▲                                              │
  │                              ┌──── IR OK ───┤
  │                              │               └──── IR timeout ──▶ ERROR
  └──── 2s delay ◀── DISPENSED ◀─┘
```

---

## Companion Web App

Open `vendbot-controller.html` in a **Chrome** browser (Web Bluetooth required).

- Works in **demo mode** without hardware (simulates dispense, auth, restock)
- Click **SCAN & CONNECT** to pair with real ESP32
- Admin PIN: `1234`

> Web Bluetooth requires Chrome on desktop or Android. iOS not supported.

---

## Customization

- **Slot count:** change `VM_MAX_SLOTS` in `vending_machine.h`
- **Dispense angle:** change `SERVO_DISPENSE_ANGLE` (default 180°)
- **IR timeout:** change `IR_TIMEOUT_MS` in `vending_machine.c`
- **Admin PIN:** change `ADMIN_PIN[]` in `vending_machine.c` and `ble_server.c`
- **Item names/prices:** edit `vm_load_defaults()` or send via BLE admin commands
