/**
 * @file led_driver_rmt.c
 * @brief WS2812 (NeoPixel) driver over the ESP32-S3 RMT TX peripheral.
 *
 * PROTOCOL
 * --------
 * A WS2812 expects, per bit, a fixed 1.25 us period encoded by the high time:
 *   logic 0 : ~0.3 us high, ~0.9 us low
 *   logic 1 : ~0.9 us high, ~0.3 us low
 * After the last bit a low period (>50 us) latches the data. Each pixel is
 * three bytes in GRB order, MSB first: 24 bits -> 24 RMT symbols.
 *
 * CHAINING
 * --------
 * Pixels are daisy-chained (DOUT -> next DIN), so a single RMT TX channel emits
 * LED_RGB_COUNT * 24 symbols in one frame; the first 24 symbols land on LED 0
 * (the on-board LED, nearest the data GPIO).
 *
 * ALGORITHM
 * ---------
 * render() (in led_rgb.c) resolves the LEDs and calls this sink with the frame.
 * The sink:
 *   1. encodes each LED's color (or black when the blink phase is off) into the
 *      shared static symbol buffer in chain order;
 *   2. transmits the whole buffer on the channel;
 *   3. waits (fixed 100 ms) for completion. 100 ms is ~3x the maximum frame time
 *      (~31 ms at the 1024-LED cap).
 *
 * MEMORY
 * ------
 * The symbol buffer is static/global (96 bytes per LED) and sized at compile
 * time; it is NOT placed on the stack. The non-DMA copy encoder streams from it.
 */
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
/** @brief Total symbols for the whole chain. */
#define LED_DRIVER_SYMBOL_COUNT (LED_RGB_COUNT * WS2812_SYMBOLS_PER_PIXEL)

/*
 * Frame-time vs render-tick guard. Each LED needs 30 us (24 bits x 1.25 us), so
 * the whole frame must fit inside the render tick. This is enforced at build
 * time because Kconfig cannot express the multiplication.
 */
#if defined(CONFIG_APP_LED_TICK_MS)
_Static_assert ((LED_RGB_COUNT * 30) <= (CONFIG_APP_LED_TICK_MS * 1000),
                "LED frame time exceeds CONFIG_APP_LED_TICK_MS");
#endif

static const char *TAG = "led_driver";

static rmt_channel_handle_t s_channel = NULL;
static rmt_encoder_handle_t s_encoder = NULL;
/* Static/global (not on the stack): can be tens of KB for long chains. */
static rmt_symbol_word_t s_symbols[LED_DRIVER_SYMBOL_COUNT];

/**
 * @brief Encode one WS2812 bit into an RMT symbol.
 *
 * A symbol has two level/duration pairs. Both bit encodings start high and end
 * low; only the high/low durations change between logic 0 and logic 1.
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
 * @brief Encode a RGB color into WS2812 GRB symbols, MSB first.
 *
 * WS2812 expects green first, then red, then blue; each byte is shifted out
 * bit 7 down to bit 0.
 *
 * @param color   Color to encode.
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
 * @brief Sink callback: encode and transmit the whole frame.
 *
 * Sequence:
 *   1. For each LED, pick its color if the blink phase is "on", else black.
 *   2. Encode it into its 24-symbol slot of the chain buffer.
 *   3. Transmit the full buffer with the copy encoder and wait for completion.
 *
 * @param frame  Resolved outputs, one per LED in ID order.
 * @param count  Number of LEDs.
 */
static void
led_driver_write (const led_rgb_output_t *frame, size_t count)
{
    for (size_t i = 0; i < count; i++)
        {
            led_rgb_color_t color = frame[i].on ? frame[i].color : (led_rgb_color_t){ 0, 0, 0 };
            led_driver_encode_color (color, &s_symbols[i * WS2812_SYMBOLS_PER_PIXEL]);
        }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    size_t bytes = count * WS2812_SYMBOLS_PER_PIXEL * sizeof (rmt_symbol_word_t);
    if (rmt_transmit (s_channel, s_encoder, s_symbols, bytes, &tx_config) != ESP_OK)
        {
            return;
        }
    rmt_tx_wait_all_done (s_channel, 100);
}

/**
 * @brief Initialise the RMT channel/encoder and register the sink.
 *
 * Sequence: create the TX channel on the GPIO, create a copy encoder (which
 * streams the pre-built symbols), enable the channel, then register this module
 * as the LED frame sink. Idempotent.
 *
 * @param gpio_num  Data GPIO.
 * @return ESP_OK on success, otherwise an RMT error.
 */
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
    ESP_LOGI (TAG, "WS2812 driver ready on GPIO %d (%d LED(s))", gpio_num, LED_RGB_COUNT);
    return ESP_OK;
}
