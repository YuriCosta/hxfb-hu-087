#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Same rate as the stock firmware; it keeps the PDM clock at 1.536 MHz */
#define AUDIO_SAMPLE_RATE   24000

esp_err_t audio_init(void);

/* Blocks until `samples` mono 16-bit samples have been read from the microphone */
esp_err_t audio_mic_read(int16_t *buf, size_t samples);

/* Amplifier power. Keep it off while recording, as the stock firmware does */
void audio_spk_enable(bool on);

/* Blocks until all mono 16-bit samples are queued for the speaker */
esp_err_t audio_spk_write(const int16_t *buf, size_t samples);
