#include "led_rgb.h"

#include <stddef.h>

/** @brief Color and blink period associated with an automatic state. */
typedef struct
{
    led_rgb_color_t color;
    uint32_t blink_ms;
} led_rgb_pattern_t;

/** @brief Automatic patterns indexed by led_rgb_state_t. */
static const led_rgb_pattern_t s_patterns[LED_RGB_STATE_COUNT] = {
    [LED_RGB_STATE_PROVISIONING] = { { 0, 0, 255 }, LED_RGB_SLOW_BLINK_MS },
    [LED_RGB_STATE_CONNECTING] = { { 255, 191, 0 }, LED_RGB_FAST_BLINK_MS },
    [LED_RGB_STATE_CONNECTED] = { { 0, 255, 0 }, 0 },
    [LED_RGB_STATE_ERROR] = { { 255, 0, 0 }, LED_RGB_FAST_BLINK_MS },
    [LED_RGB_STATE_CUSTOM] = { { 0, 0, 0 }, 0 },
};

static led_rgb_state_t s_state = LED_RGB_STATE_PROVISIONING;
static led_rgb_color_t s_custom_color = { 0, 0, 0 };
static uint32_t s_custom_blink_ms = 0;
static led_rgb_sink_t s_sink = NULL;

void
led_rgb_init (void)
{
    s_state = LED_RGB_STATE_PROVISIONING;
    s_custom_color = (led_rgb_color_t){ 0, 0, 0 };
    s_custom_blink_ms = 0;
}

void
led_rgb_set_sink (led_rgb_sink_t sink)
{
    s_sink = sink;
}

void
led_rgb_set_state (led_rgb_state_t state)
{
    if (state >= LED_RGB_STATE_COUNT || state == LED_RGB_STATE_CUSTOM)
        {
            return;
        }
    s_state = state;
}

led_rgb_state_t
led_rgb_get_state (void)
{
    return s_state;
}

esp_err_t
led_rgb_set_custom (led_rgb_color_t color, uint32_t blink_ms)
{
    if (s_state != LED_RGB_STATE_CONNECTED)
        {
            return ESP_ERR_INVALID_STATE;
        }
    if (blink_ms > LED_RGB_MAX_BLINK_MS)
        {
            return ESP_ERR_INVALID_ARG;
        }
    s_custom_color = color;
    s_custom_blink_ms = blink_ms;
    s_state = LED_RGB_STATE_CUSTOM;
    return ESP_OK;
}

esp_err_t
led_rgb_clear_custom (void)
{
    if (s_state != LED_RGB_STATE_CUSTOM)
        {
            return ESP_ERR_INVALID_STATE;
        }
    s_state = LED_RGB_STATE_CONNECTED;
    return ESP_OK;
}

bool
led_rgb_is_custom (void)
{
    return s_state == LED_RGB_STATE_CUSTOM;
}

/**
 * @brief Evaluate a blink pattern at a given time.
 *
 * @param blink_ms  Period in milliseconds; 0 means solid on.
 * @param now_ms    Time in milliseconds.
 * @return true if the LED should be lit at now_ms.
 */
static bool
led_rgb_blink_on (uint32_t blink_ms, uint32_t now_ms)
{
    if (blink_ms == 0)
        {
            return true;
        }
    return (now_ms % blink_ms) < (blink_ms / 2);
}

void
led_rgb_get_output (uint32_t now_ms, led_rgb_output_t *out)
{
    if (out == NULL)
        {
            return;
        }

    const led_rgb_pattern_t *pattern = &s_patterns[s_state];
    led_rgb_color_t color = pattern->color;
    uint32_t blink_ms = pattern->blink_ms;

    if (s_state == LED_RGB_STATE_CUSTOM)
        {
            color = s_custom_color;
            blink_ms = s_custom_blink_ms;
        }

    out->state = s_state;
    out->color = color;
    out->blink_ms = blink_ms;
    out->on = led_rgb_blink_on (blink_ms, now_ms);
}

void
led_rgb_render (uint32_t now_ms)
{
    if (s_sink == NULL)
        {
            return;
        }
    led_rgb_output_t out;
    led_rgb_get_output (now_ms, &out);
    s_sink (&out);
}
