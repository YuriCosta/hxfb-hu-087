#include <string.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "oled.h"

static const char *TAG = "oled";

static esp_lcd_panel_handle_t s_panel;

/* SSD1306 native layout: 8 pages of 8 rows, one byte per column, LSB on top */
static uint8_t s_fb[OLED_WIDTH * OLED_HEIGHT / 8];

/* Classic 5x7 font, one byte per column, LSB on top */
static const char FONT_CHARS[] = " .:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const uint8_t FONT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
};

esp_err_t oled_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_OLED_SDA,
        .scl_io_num = BOARD_OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus");

    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = BOARD_OLED_I2C_ADDR,
        .scl_speed_hz = BOARD_OLED_I2C_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io");

    esp_lcd_panel_ssd1306_config_t ssd1306_cfg = {
        .height = OLED_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &ssd1306_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &s_panel), TAG, "ssd1306");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, BOARD_OLED_MIRROR_X, BOARD_OLED_MIRROR_Y), TAG, "mirror");

    oled_clear();
    ESP_RETURN_ON_ERROR(oled_flush(), TAG, "first frame");
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

void oled_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void oled_pixel(int x, int y)
{
    if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
        s_fb[(y / 8) * OLED_WIDTH + x] |= 1 << (y % 8);
    }
}

void oled_vline(int x, int y0, int y1)
{
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        oled_pixel(x, y);
    }
}

void oled_hline(int x0, int x1, int y)
{
    for (int x = x0; x <= x1; x++) {
        oled_pixel(x, y);
    }
}

void oled_rect(int x0, int y0, int x1, int y1, bool fill)
{
    if (fill) {
        for (int x = x0; x <= x1; x++) {
            oled_vline(x, y0, y1);
        }
        return;
    }
    oled_hline(x0, x1, y0);
    oled_hline(x0, x1, y1);
    oled_vline(x0, y0, y1);
    oled_vline(x1, y0, y1);
}

int oled_text(int x, int row, const char *s)
{
    if (row < 0 || row >= OLED_HEIGHT / 8) {
        return x;
    }
    for (; *s; s++) {
        const char *p = strchr(FONT_CHARS, *s);
        const uint8_t *glyph = FONT[p ? p - FONT_CHARS : 0];
        for (int c = 0; c < 5; c++, x++) {
            if (x >= 0 && x < OLED_WIDTH) {
                s_fb[row * OLED_WIDTH + x] |= glyph[c];
            }
        }
        x++;    /* 1 pixel spacing */
    }
    return x;
}

esp_err_t oled_flush(void)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, s_fb);
}
