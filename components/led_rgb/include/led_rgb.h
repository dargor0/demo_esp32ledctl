/**
 * @file led_rgb.h
 * @brief RGB LED state machine for the ESP32-S3 DevKit.
 *
 * The LED reflects the device connection state using distinct colors and blink
 * patterns:
 *   - provisioning : slow blue blink
 *   - connecting   : fast amber blink
 *   - connected    : solid green
 *   - error        : fast red blink
 *   - custom       : web-controlled color and blink rate
 *
 * The custom state can only be entered while the device is connected. A BOOT
 * short press or an explicit call to led_rgb_clear_custom() returns the LED to
 * the solid green connected state.
 *
 * The state machine is pure logic: it never touches hardware. A sink callback
 * (see led_rgb_set_sink()) receives the resolved output each render, allowing
 * the RMT driver or a test double to consume it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Default slow blink period in milliseconds (~1 Hz). */
#define LED_RGB_SLOW_BLINK_MS 500
/** @brief Default fast blink period in milliseconds (~5 Hz). */
#define LED_RGB_FAST_BLINK_MS 100
/** @brief Largest accepted custom blink period in milliseconds. */
#define LED_RGB_MAX_BLINK_MS 60000

    /** @brief LED operating states. */
    typedef enum
    {
        LED_RGB_STATE_PROVISIONING = 0, /**< Waiting for WiFi credentials. */
        LED_RGB_STATE_CONNECTING,       /**< Connecting to WiFi. */
        LED_RGB_STATE_CONNECTED,        /**< Connected, no custom override. */
        LED_RGB_STATE_ERROR,            /**< Provisioning or WiFi error. */
        LED_RGB_STATE_CUSTOM,           /**< Web-controlled color and blink. */
        LED_RGB_STATE_COUNT,            /**< Number of states, not a real state. */
    } led_rgb_state_t;

    /** @brief RGB color, 0-255 per channel. */
    typedef struct
    {
        uint8_t r; /**< Red channel. */
        uint8_t g; /**< Green channel. */
        uint8_t b; /**< Blue channel. */
    } led_rgb_color_t;

    /** @brief Resolved LED output for a point in time. */
    typedef struct
    {
        led_rgb_state_t state; /**< State that produced this output. */
        led_rgb_color_t color; /**< Color before the blink on/off is applied. */
        uint32_t blink_ms;     /**< Blink period, 0 means solid. */
        bool on;               /**< Whether the LED is lit at this instant. */
    } led_rgb_output_t;

    /**
     * @brief Callback receiving the resolved output on every render.
     *
     * @param out  Resolved output; valid only for the duration of the call.
     */
    typedef void (*led_rgb_sink_t) (const led_rgb_output_t *out);

    /**
     * @brief Reset the state machine to its power-on defaults.
     */
    void led_rgb_init (void);

    /**
     * @brief Register the output sink that consumes rendered frames.
     *
     * @param sink  Callback invoked by led_rgb_render(), or NULL to detach.
     */
    void led_rgb_set_sink (led_rgb_sink_t sink);

    /**
     * @brief Set the automatic state.
     *
     * Setting any state other than LED_RGB_STATE_CUSTOM clears a custom override.
     * Out-of-range values are ignored.
     *
     * @param state  Target automatic state.
     */
    void led_rgb_set_state (led_rgb_state_t state);

    /**
     * @brief Get the current state.
     *
     * @return The current LED state.
     */
    led_rgb_state_t led_rgb_get_state (void);

    /**
     * @brief Enter the custom state with a user color and blink rate.
     *
     * Only allowed while the current state is LED_RGB_STATE_CONNECTED.
     *
     * @param color     Desired color.
     * @param blink_ms  Blink period in milliseconds; 0 means solid.
     * @return
     *   - ESP_OK on success.
     *   - ESP_ERR_INVALID_STATE if the device is not connected.
     *   - ESP_ERR_INVALID_ARG if blink_ms exceeds LED_RGB_MAX_BLINK_MS.
     */
    esp_err_t led_rgb_set_custom (led_rgb_color_t color, uint32_t blink_ms);

    /**
     * @brief Clear a custom override and return to the connected state.
     *
     * @return
     *   - ESP_OK on success.
     *   - ESP_ERR_INVALID_STATE if the LED is not currently custom.
     */
    esp_err_t led_rgb_clear_custom (void);

    /**
     * @brief Check whether the custom state is active.
     *
     * @return true if the LED is in the custom state.
     */
    bool led_rgb_is_custom (void);

    /**
     * @brief Resolve the output for a given time without invoking the sink.
     *
     * @param now_ms  Monotonic time in milliseconds used for blink evaluation.
     * @param out     Destination for the resolved output; must not be NULL.
     */
    void led_rgb_get_output (uint32_t now_ms, led_rgb_output_t *out);

    /**
     * @brief Resolve the output and forward it to the registered sink.
     *
     * @param now_ms  Monotonic time in milliseconds used for blink evaluation.
     */
    void led_rgb_render (uint32_t now_ms);

#ifdef __cplusplus
}
#endif
