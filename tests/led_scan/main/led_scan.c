#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RMT_RESOLUTION_HZ 10000000
#define WS2812_T0H 3
#define WS2812_T0L 9
#define WS2812_T1H 9
#define WS2812_T1L 3

typedef struct {
    int gpio;
    const char *color;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} candidate_t;

// One distinct color per candidate pin. Watch the LED and report the color.
static const candidate_t CANDIDATES[] = {
    { 38, "RED", 255, 0, 0 },       { 48, "GREEN", 0, 255, 0 },
    { 47, "BLUE", 0, 0, 255 },      { 21, "YELLOW", 255, 255, 0 },
    { 14, "MAGENTA", 255, 0, 255 }, { 8, "CYAN", 0, 255, 255 },
    { 17, "WHITE", 255, 255, 255 }, { 18, "ORANGE", 255, 128, 0 },
    { 16, "PURPLE", 128, 0, 255 },  { 15, "SPRING", 0, 255, 128 },
};
#define CANDIDATE_COUNT (sizeof(CANDIDATES) / sizeof(CANDIDATES[0]))

static void encode_byte(uint8_t byte, rmt_symbol_word_t *symbols, size_t *index)
{
    for (int bit = 7; bit >= 0; bit--) {
        if (byte & (1 << bit)) {
            symbols[(*index)++] =
                (rmt_symbol_word_t) { .level0 = 1, .duration0 = WS2812_T1H,
                                      .level1 = 0, .duration1 = WS2812_T1L };
        } else {
            symbols[(*index)++] =
                (rmt_symbol_word_t) { .level0 = 1, .duration0 = WS2812_T0H,
                                      .level1 = 0, .duration1 = WS2812_T0L };
        }
    }
}

static void send_color(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, uint8_t r,
                       uint8_t g, uint8_t b)
{
    rmt_symbol_word_t symbols[24];
    size_t index = 0;
    const uint8_t grb[3] = { g, r, b };
    for (int i = 0; i < 3; i++) {
        encode_byte(grb[i], symbols, &index);
    }
    rmt_transmit_config_t config = { .loop_count = 0 };
    if (rmt_transmit(channel, encoder, symbols, sizeof(symbols), &config) == ESP_OK) {
        rmt_tx_wait_all_done(channel, 100);
    }
}

static void hold_color(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, const candidate_t *c,
                       uint32_t on_ms, uint32_t off_ms)
{
    for (uint32_t t = 0; t < on_ms; t += 50) {
        send_color(channel, encoder, c->r, c->g, c->b);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    for (uint32_t t = 0; t < off_ms; t += 50) {
        send_color(channel, encoder, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    ESP_LOGI("led_scan", "RGB pin scan started. Watch the LED and note the COLOR.");
    for (;;) {
        for (size_t i = 0; i < CANDIDATE_COUNT; i++) {
            const candidate_t *c = &CANDIDATES[i];
            rmt_channel_handle_t channel = NULL;
            rmt_encoder_handle_t encoder = NULL;

            const rmt_tx_channel_config_t channel_config = {
                .clk_src = RMT_CLK_SRC_DEFAULT,
                .gpio_num = c->gpio,
                .mem_block_symbols = 64,
                .resolution_hz = RMT_RESOLUTION_HZ,
                .trans_queue_depth = 4,
            };
            const rmt_copy_encoder_config_t encoder_config = {};
            if (rmt_new_tx_channel(&channel_config, &channel) != ESP_OK ||
                rmt_new_copy_encoder(&encoder_config, &encoder) != ESP_OK) {
                ESP_LOGW("led_scan", "GPIO %d: channel create failed", c->gpio);
                continue;
            }
            rmt_enable(channel);
            ESP_LOGI("led_scan", "COLOR %-7s -> GPIO %d", c->color, c->gpio);
            hold_color(channel, encoder, c, 2000, 500);
            rmt_disable(channel);
            rmt_del_channel(channel);
            rmt_del_encoder(encoder);
        }
    }
}
