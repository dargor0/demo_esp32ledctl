#include "led_driver.h"

#include <string.h>

#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "led_rgb.h"

/** @brief RMT resolution, 10 MHz gives one tick per 0.1 us. */
#define LED_DRIVER_RESOLUTION_HZ 10000000
/** @brief RMT memory block size in symbols (minimum for non-DMA channels). */
#define LED_DRIVER_MEM_BLOCK_SYMBOLS 64
/** @brief WS2812 bit timing in ticks for a logic 0 (0.3 us high, 0.9 us low). */
#define WS2812_T0H_TICKS 3
#define WS2812_T0L_TICKS 9
/** @brief WS2812 bit timing in ticks for a logic 1 (0.9 us high, 0.3 us low). */
#define WS2812_T1H_TICKS 9
#define WS2812_T1L_TICKS 3
/** @brief Number of RMT symbols per pixel (3 bytes x 8 bits). */
#define WS2812_SYMBOLS_PER_PIXEL 24

static const char *TAG = "led_driver";

static rmt_channel_handle_t s_channel = NULL;
static rmt_encoder_handle_t s_encoder = NULL;

/**
 * @brief Encode one WS2812 bit into an RMT symbol.
 *
 * @param one  true for a logic 1, false for a logic 0.
 * @return The encoded symbol.
 */
static rmt_symbol_word_t
led_driver_encode_bit (bool one)
{
    if (one)
        {
            return (rmt_symbol_word_t){ .level0 = 1,
                                        .duration0 = WS2812_T1H_TICKS,
                                        .level1 = 0,
                                        .duration1 = WS2812_T1L_TICKS };
        }
    return (rmt_symbol_word_t){
        .level0 = 1, .duration0 = WS2812_T0H_TICKS, .level1 = 0, .duration1 = WS2812_T0L_TICKS
    };
}

/**
 * @brief Encode a RGB color into WS2812 GRB symbols.
 *
 * @param color  Color to encode.
 * @param symbols Destination array of WS2812_SYMBOLS_PER_PIXEL symbols.
 */
static void
led_driver_encode_color (led_rgb_color_t color, rmt_symbol_word_t *symbols)
{
    const uint8_t grb[3] = { color.g, color.r, color.b };
    size_t index = 0;

    for (size_t byte = 0; byte < sizeof (grb); byte++)
        {
            for (int bit = 7; bit >= 0; bit--)
                {
                    symbols[index++] = led_driver_encode_bit ((grb[byte] >> bit) & 0x1);
                }
        }
}

/**
 * @brief Sink callback that pushes the resolved output to the LED.
 *
 * @param out  Resolved LED output.
 */
static void
led_driver_write (const led_rgb_output_t *out)
{
    rmt_symbol_word_t symbols[WS2812_SYMBOLS_PER_PIXEL];
    led_rgb_color_t color = out->on ? out->color : (led_rgb_color_t){ 0, 0, 0 };

    led_driver_encode_color (color, symbols);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    if (rmt_transmit (s_channel, s_encoder, symbols, sizeof (symbols), &tx_config) != ESP_OK)
        {
            return;
        }
    rmt_tx_wait_all_done (s_channel, 100);
}

esp_err_t
led_driver_init (int gpio_num)
{
    if (s_channel != NULL)
        {
            return ESP_OK;
        }

    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num,
        .mem_block_symbols = LED_DRIVER_MEM_BLOCK_SYMBOLS,
        .resolution_hz = LED_DRIVER_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR (rmt_new_tx_channel (&channel_config, &s_channel), TAG,
                         "create RMT TX channel");

    rmt_copy_encoder_config_t encoder_config = {};
    ESP_RETURN_ON_ERROR (rmt_new_copy_encoder (&encoder_config, &s_encoder), TAG,
                         "create RMT copy encoder");

    ESP_RETURN_ON_ERROR (rmt_enable (s_channel), TAG, "enable RMT TX channel");

    led_rgb_set_sink (led_driver_write);
    ESP_LOGI (TAG, "WS2812 driver ready on GPIO %d", gpio_num);
    return ESP_OK;
}
