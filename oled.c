#include "oled.h"
#include "vending_machine.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OLED";

// ─── SSD1306 128×64 framebuffer ───────────────────────────────────────────────

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)

static uint8_t s_fb[OLED_PAGES][OLED_WIDTH];

// ─── I2C helpers ──────────────────────────────────────────────────────────────

static esp_err_t oled_write_cmd(uint8_t cmd) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x00, true); // Co=0, D/C#=0 → command
    i2c_master_write_byte(h, cmd, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, h, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(h);
    return ret;
}

static esp_err_t oled_flush(void) {
    // Set col/page address to full screen
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(7);

    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x40, true); // D/C#=1 → data
    for (int p = 0; p < OLED_PAGES; p++) {
        i2c_master_write(h, s_fb[p], OLED_WIDTH, true);
    }
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    return ret;
}

// ─── 5×7 font (ASCII 32–126) — minimal subset ────────────────────────────────
// Each character is 5 columns of 8 bits (1 bit = 1 pixel row)

static const uint8_t FONT5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '\''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x14,0x08,0x3E,0x08,0x14}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
};

// ─── Drawing primitives ───────────────────────────────────────────────────────

static void fb_clear(void) {
    memset(s_fb, 0, sizeof(s_fb));
}

static void fb_draw_char(int x, int page, char c) {
    if (c < 32 || c > 122) c = '?';
    const uint8_t *glyph = FONT5x7[c - 32];
    for (int col = 0; col < 5 && (x + col) < OLED_WIDTH; col++) {
        s_fb[page][x + col] = glyph[col];
    }
}

static void fb_draw_string(int x, int page, const char *str) {
    while (*str && x < OLED_WIDTH) {
        fb_draw_char(x, page, *str++);
        x += 6; // 5px char + 1px spacing
    }
}

static void fb_draw_hline(int x0, int x1, int page, uint8_t pattern) {
    for (int x = x0; x <= x1 && x < OLED_WIDTH; x++) {
        s_fb[page][x] = pattern;
    }
}

static void fb_invert_row(int page) {
    for (int x = 0; x < OLED_WIDTH; x++) {
        s_fb[page][x] ^= 0xFF;
    }
}

// ─── SSD1306 Init sequence ────────────────────────────────────────────────────

esp_err_t oled_init(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA,
        .scl_io_num       = I2C_MASTER_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    // SSD1306 initialization sequence
    const uint8_t init_cmds[] = {
        0xAE,       // display off
        0xD5,0x80,  // set display clock div
        0xA8,0x3F,  // set multiplex ratio 64
        0xD3,0x00,  // display offset 0
        0x40,       // start line 0
        0x8D,0x14,  // charge pump on
        0x20,0x00,  // horizontal addressing mode
        0xA1,       // segment re-map
        0xC8,       // COM scan direction reversed
        0xDA,0x12,  // COM pins hardware config
        0x81,0xCF,  // contrast
        0xD9,0xF1,  // pre-charge period
        0xDB,0x40,  // VCOMH deselect level
        0xA4,       // display from RAM
        0xA6,       // normal display (not inverted)
        0xAF,       // display on
    };
    for (int i = 0; i < sizeof(init_cmds); i++) {
        oled_write_cmd(init_cmds[i]);
    }

    fb_clear();
    oled_flush();
    ESP_LOGI(TAG, "OLED init OK");
    return ESP_OK;
}

// ─── Screen helpers ───────────────────────────────────────────────────────────

void oled_show_idle(void) {
    fb_clear();
    fb_draw_string(10, 0, "  VendBot ESP32");
    fb_draw_hline(0, 127, 1, 0xFF);
    fb_draw_string(0, 2, "  Ready to serve");
    fb_draw_string(0, 4, " Connect via BLE");
    fb_draw_string(0, 6, "  \"VendBot-ESP32\"");
    oled_flush();
}

void oled_show_selected(uint8_t slot, const char *name, uint16_t price_cents) {
    char buf[24];
    fb_clear();
    snprintf(buf, sizeof(buf), "Slot %d: %s", slot + 1, name);
    fb_draw_string(0, 0, buf);
    fb_draw_hline(0, 127, 1, 0xFF);
    fb_draw_string(0, 3, "Price:");
    snprintf(buf, sizeof(buf), "$%d.%02d", price_cents / 100, price_cents % 100);
    fb_draw_string(50, 3, buf);
    fb_draw_string(0, 5, "Send DISPENSE cmd");
    oled_flush();
}

void oled_show_dispensing(uint8_t slot) {
    char buf[24];
    fb_clear();
    fb_draw_string(20, 1, "DISPENSING...");
    snprintf(buf, sizeof(buf), "Slot %d", slot + 1);
    fb_draw_string(45, 3, buf);
    fb_draw_string(10, 5, "Please wait...");
    oled_flush();
}

void oled_show_success(uint8_t slot) {
    fb_clear();
    fb_draw_string(30, 1, "SUCCESS!");
    fb_draw_hline(0, 127, 2, 0xFF);
    fb_draw_string(5, 4, "Enjoy your item :)");
    oled_flush();
}

void oled_show_error(vm_error_t err) {
    fb_clear();
    fb_draw_string(35, 0, "ERROR");
    fb_draw_hline(0, 127, 1, 0xFF);
    const char *msg;
    switch (err) {
        case VM_ERR_EMPTY:        msg = "Slot is empty!";   break;
        case VM_ERR_JAM:          msg = "Dispense jam!";    break;
        case VM_ERR_AUTH:         msg = "Bad admin PIN!";   break;
        case VM_ERR_INVALID_SLOT: msg = "Invalid slot!";    break;
        default:                  msg = "Unknown error";    break;
    }
    fb_draw_string(0, 3, msg);
    fb_draw_string(0, 5, "BLE: reset error");
    oled_flush();
}

void oled_show_admin(void) {
    fb_clear();
    fb_draw_string(20, 0, "ADMIN MODE");
    fb_draw_hline(0, 127, 1, 0xFF);
    fb_draw_string(0, 2, "Restock / Price");
    fb_draw_string(0, 4, "via BLE app");
    oled_flush();
}

void oled_show_inventory(vm_slot_t slots[], uint8_t count) {
    fb_clear();
    fb_draw_string(10, 0, "Inventory");
    fb_draw_hline(0, 127, 1, 0x55);
    char buf[22];
    for (int i = 0; i < count && i < 6; i++) {
        snprintf(buf, sizeof(buf), "%d:%-8s %d/$%d",
                 i + 1, slots[i].name, slots[i].count,
                 slots[i].price_cents / 100);
        fb_draw_string(0, 2 + i, buf);
    }
    oled_flush();
}
