/*
 * HXFB-HU-087 hardware test.
 *
 * The OLED shows the live microphone waveform. Hold the button to record
 * (up to 2 s); 200 ms after it is released (or the time runs out) the
 * recording is played back on the speaker. Exercises display, button,
 * microphone and speaker.
 */
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio.h"
#include "board.h"
#include "oled.h"

#define REC_MAX_MS          2000
#define PLAY_DELAY_MS       200
#define REC_MAX_SAMPLES     (AUDIO_SAMPLE_RATE * REC_MAX_MS / 1000)

#define MIC_CHUNK           240     /* 10 ms */
#define PLAY_CHUNK          240

#define PLAY_PEAK           20000   /* playback is normalized to this peak... */
#define PLAY_MAX_GAIN       16      /* ...but never amplified more than this */

#define SCOPE_SAMPLES       1024    /* ~43 ms of audio across the screen */
#define SCOPE_MIN_PEAK      300     /* keeps silence from being blown up into noise */
#define SCOPE_TOP           10      /* rows 0..8 hold the status line */
#define SCOPE_MID           ((SCOPE_TOP + OLED_HEIGHT - 1) / 2)
#define SCOPE_HALF          ((OLED_HEIGHT - 1 - SCOPE_TOP) / 2)
#define DISPLAY_PERIOD_MS   40

typedef enum {
    ST_IDLE,
    ST_RECORDING,
    ST_WAIT,        /* recording done, waiting PLAY_DELAY_MS */
    ST_PLAYING,
} app_state_t;

static const char *TAG = "hu087_test";

static volatile app_state_t s_state = ST_IDLE;
static int16_t *s_rec;
static volatile size_t s_rec_len;
static volatile size_t s_play_pos;

/* Ring of the latest microphone samples, drawn by the display task */
static int16_t s_scope[SCOPE_SAMPLES];
static size_t s_scope_pos;
static portMUX_TYPE s_scope_lock = portMUX_INITIALIZER_UNLOCKED;

static int16_t clamp16(int32_t v)
{
    return v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : v;
}

static void mic_task(void *arg)
{
    (void)arg;
    int16_t buf[MIC_CHUNK];
    int32_t prev_in = 0, prev_out = 0;
    for (;;) {
        if (audio_mic_read(buf, MIC_CHUNK) != ESP_OK) {
            continue;
        }
        /* Remove the DC offset of the PDM decimator (one-pole high-pass, ~60 Hz) */
        for (int i = 0; i < MIC_CHUNK; i++) {
            int32_t in = buf[i];
            prev_out = in - prev_in + prev_out * 252 / 256;
            prev_in = in;
            buf[i] = clamp16(prev_out);
        }

        taskENTER_CRITICAL(&s_scope_lock);
        for (int i = 0; i < MIC_CHUNK; i++) {
            s_scope[s_scope_pos] = buf[i];
            s_scope_pos = (s_scope_pos + 1) % SCOPE_SAMPLES;
        }
        taskEXIT_CRITICAL(&s_scope_lock);

        if (s_state == ST_RECORDING && s_rec_len < REC_MAX_SAMPLES) {
            size_t n = REC_MAX_SAMPLES - s_rec_len;
            n = n < MIC_CHUNK ? n : MIC_CHUNK;
            memcpy(s_rec + s_rec_len, buf, n * sizeof(int16_t));
            s_rec_len += n;
        }
    }
}

static void draw_progress(int x0, size_t done, size_t total)
{
    oled_rect(x0, 1, OLED_WIDTH - 1, 6, false);
    if (total > 0 && done > 0) {
        int w = (OLED_WIDTH - 2 - x0) * done / total;
        oled_rect(x0 + 1, 2, x0 + w, 5, true);
    }
}

static void draw_status(void)
{
    switch (s_state) {
    case ST_IDLE:
        oled_text(0, 0, "HOLD BUTTON TO REC");
        break;
    case ST_RECORDING:
        draw_progress(oled_text(0, 0, "REC") + 4, s_rec_len, REC_MAX_SAMPLES);
        break;
    case ST_WAIT:
    case ST_PLAYING:
        draw_progress(oled_text(0, 0, "PLAY") + 4, s_play_pos, s_rec_len);
        break;
    }
    oled_hline(0, OLED_WIDTH - 1, 8);
}

static void display_task(void *arg)
{
    (void)arg;
    static int16_t snap[SCOPE_SAMPLES];
    static int16_t lo[OLED_WIDTH], hi[OLED_WIDTH];
    const int per_col = SCOPE_SAMPLES / OLED_WIDTH;
    int peak = SCOPE_MIN_PEAK;

    for (;;) {
        taskENTER_CRITICAL(&s_scope_lock);
        size_t start = s_scope_pos;
        memcpy(snap, s_scope + start, (SCOPE_SAMPLES - start) * sizeof(int16_t));
        memcpy(snap + SCOPE_SAMPLES - start, s_scope, start * sizeof(int16_t));
        taskEXIT_CRITICAL(&s_scope_lock);

        /* Each column shows the min..max of its samples */
        int frame_peak = 0;
        for (int x = 0; x < OLED_WIDTH; x++) {
            const int16_t *s = snap + x * per_col;
            lo[x] = hi[x] = s[0];
            for (int i = 1; i < per_col; i++) {
                lo[x] = s[i] < lo[x] ? s[i] : lo[x];
                hi[x] = s[i] > hi[x] ? s[i] : hi[x];
            }
            int a = abs(lo[x]) > abs(hi[x]) ? abs(lo[x]) : abs(hi[x]);
            frame_peak = a > frame_peak ? a : frame_peak;
        }
        /* Auto scale: jump up to louder sounds, shrink back slowly */
        peak = frame_peak > peak ? frame_peak : peak - peak / 16;
        peak = peak < SCOPE_MIN_PEAK ? SCOPE_MIN_PEAK : peak;

        oled_clear();
        draw_status();
        for (int x = 0; x < OLED_WIDTH; x++) {
            oled_vline(x, SCOPE_MID - hi[x] * SCOPE_HALF / peak, SCOPE_MID - lo[x] * SCOPE_HALF / peak);
        }
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

/* Small speakers and PDM mics: bring quiet recordings up to a usable level */
static void normalize(int16_t *buf, size_t len)
{
    int peak = 1;
    for (size_t i = 0; i < len; i++) {
        int a = abs(buf[i]);
        peak = a > peak ? a : peak;
    }
    int gain_q8 = PLAY_PEAK * 256 / peak;
    gain_q8 = gain_q8 > PLAY_MAX_GAIN * 256 ? PLAY_MAX_GAIN * 256 : gain_q8;
    for (size_t i = 0; i < len; i++) {
        buf[i] = clamp16(buf[i] * gain_q8 / 256);
    }
    ESP_LOGI(TAG, "peak %d, playback gain %d.%02d", peak, gain_q8 / 256, (gain_q8 % 256) * 100 / 256);
}

static void play_recording(void)
{
    size_t len = s_rec_len;
    normalize(s_rec, len);
    s_state = ST_PLAYING;
    ESP_LOGI(TAG, "playing %u ms", (unsigned)(len * 1000 / AUDIO_SAMPLE_RATE));
    for (size_t pos = 0; pos < len; pos += PLAY_CHUNK) {
        size_t n = len - pos < PLAY_CHUNK ? len - pos : PLAY_CHUNK;
        if (audio_spk_write(s_rec + pos, n) != ESP_OK) {
            break;
        }
        s_play_pos = pos + n;
    }
    /* Let the DMA buffers drain before cutting the amplifier */
    vTaskDelay(pdMS_TO_TICKS(100));
    audio_spk_enable(false);
}

/* Debounced button level, sampled every 10 ms */
static bool button_read(void)
{
    static bool state;
    static int count;
    bool raw = gpio_get_level(BOARD_BUTTON) == 0;
    if (raw == state) {
        count = 0;
    } else if (++count >= 3) {
        state = raw;
        count = 0;
    }
    return state;
}

void app_main(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = BIT64(BOARD_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    s_rec = heap_caps_malloc(REC_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(s_rec);
    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(oled_init());

    xTaskCreate(mic_task, "mic", 4096, NULL, 6, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "ready: hold the button (GPIO%d) to record", BOARD_BUTTON);

    bool was_pressed = false;
    int64_t play_at = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        bool pressed = button_read();

        switch (s_state) {
        case ST_IDLE:
            if (pressed && !was_pressed) {
                s_rec_len = 0;
                s_play_pos = 0;
                s_state = ST_RECORDING;
                ESP_LOGI(TAG, "recording");
            }
            break;
        case ST_RECORDING:
            if (!pressed || s_rec_len >= REC_MAX_SAMPLES) {
                s_state = ST_WAIT;
                play_at = esp_timer_get_time() + PLAY_DELAY_MS * 1000;
                audio_spk_enable(true);     /* the delay also lets the amplifier settle */
                ESP_LOGI(TAG, "recorded %u ms%s", (unsigned)(s_rec_len * 1000 / AUDIO_SAMPLE_RATE),
                         pressed ? " (time limit)" : "");
            }
            break;
        case ST_WAIT:
            if (esp_timer_get_time() >= play_at) {
                play_recording();
                s_state = ST_IDLE;
            }
            break;
        case ST_PLAYING:
            break;
        }
        /* A new recording needs a new press, even if the button was held through playback */
        was_pressed = pressed;
    }
}
