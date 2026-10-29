/*
 * HXFB-HU-087 pin map (ESP32-S3).
 *
 * Recovered from the stock firmware (xiaozhi v1.5.6, board "hxfb-hu-087").
 * See the README at the repository root for details.
 */
#pragma once

#include "driver/gpio.h"

/* OLED: SSD1306 128x64 on I2C, no reset line */
#define BOARD_OLED_SDA          GPIO_NUM_41
#define BOARD_OLED_SCL          GPIO_NUM_42
#define BOARD_OLED_I2C_ADDR     0x3C
#define BOARD_OLED_I2C_HZ       400000
#define BOARD_OLED_WIDTH        128
#define BOARD_OLED_HEIGHT       64
#define BOARD_OLED_MIRROR_X     true    /* the stock firmware mirrors both axes */
#define BOARD_OLED_MIRROR_Y     true

/* Speaker: I2S (standard/Philips) into a digital class-D amplifier */
#define BOARD_SPK_BCLK          GPIO_NUM_15
#define BOARD_SPK_WS            GPIO_NUM_16
#define BOARD_SPK_DOUT          GPIO_NUM_7
#define BOARD_SPK_EN            GPIO_NUM_17     /* amplifier enable, active high */

/* Microphone: PDM (only I2S0 supports PDM input on the ESP32-S3) */
#define BOARD_MIC_CLK           GPIO_NUM_5
#define BOARD_MIC_DATA          GPIO_NUM_6

/* Single user button, active low */
#define BOARD_BUTTON            GPIO_NUM_18
