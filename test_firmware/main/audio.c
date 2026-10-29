#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "audio.h"

#define SPK_CHUNK   128     /* samples converted per i2s_channel_write() */

static const char *TAG = "audio";

static i2s_chan_handle_t s_mic;
static i2s_chan_handle_t s_spk;

static esp_err_t mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_mic), TAG, "mic channel");

    i2s_pdm_rx_config_t cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = BOARD_MIC_CLK,
            .din = BOARD_MIC_DATA,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_mic, &cfg), TAG, "mic init");
    return i2s_channel_enable(s_mic);
}

static esp_err_t spk_init(void)
{
    gpio_config_t en = {
        .pin_bit_mask = BIT64(BOARD_SPK_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&en), TAG, "amp enable gpio");
    gpio_set_level(BOARD_SPK_EN, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;     /* send silence when there is nothing to play */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_spk, NULL), TAG, "speaker channel");

    /* 32-bit Philips like the stock firmware. Stereo with the same sample in both
     * slots, so it plays whichever channel the amplifier is strapped to */
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_SPK_BCLK,
            .ws = BOARD_SPK_WS,
            .dout = BOARD_SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_spk, &cfg), TAG, "speaker init");
    return i2s_channel_enable(s_spk);
}

esp_err_t audio_init(void)
{
    ESP_RETURN_ON_ERROR(mic_init(), TAG, "microphone");
    return spk_init();
}

esp_err_t audio_mic_read(int16_t *buf, size_t samples)
{
    size_t got;
    return i2s_channel_read(s_mic, buf, samples * sizeof(int16_t), &got, portMAX_DELAY);
}

void audio_spk_enable(bool on)
{
    gpio_set_level(BOARD_SPK_EN, on);
}

esp_err_t audio_spk_write(const int16_t *buf, size_t samples)
{
    int32_t frames[2 * SPK_CHUNK];
    while (samples > 0) {
        size_t n = samples < SPK_CHUNK ? samples : SPK_CHUNK;
        for (size_t i = 0; i < n; i++) {
            frames[2 * i] = frames[2 * i + 1] = (int32_t)buf[i] << 16;
        }
        size_t written;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_spk, frames, n * 2 * sizeof(int32_t), &written, portMAX_DELAY),
                            TAG, "speaker write");
        buf += n;
        samples -= n;
    }
    return ESP_OK;
}
