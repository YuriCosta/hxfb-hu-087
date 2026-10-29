#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "board.h"

#define OLED_WIDTH  BOARD_OLED_WIDTH
#define OLED_HEIGHT BOARD_OLED_HEIGHT

esp_err_t oled_init(void);

/* Drawing happens in a RAM frame buffer; oled_flush() sends it to the panel */
void oled_clear(void);
void oled_pixel(int x, int y);
void oled_vline(int x, int y0, int y1);
void oled_hline(int x0, int x1, int y);
void oled_rect(int x0, int y0, int x1, int y1, bool fill);

/* 5x7 font (A-Z, 0-9, space . :), drawn in an 8-pixel text row (0..7).
 * Returns the x position after the text */
int oled_text(int x, int row, const char *s);

esp_err_t oled_flush(void);
