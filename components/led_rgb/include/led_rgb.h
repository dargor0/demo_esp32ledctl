/**
 * @file led_rgb.h
 * @brief RGB LED state machine for one or more chained WS2812 LEDs.
 *
 * LEDs are addressed by a zero-based ID (0 = the on-board LED, nearest the data
 * GPIO). The LED follows the device connection state by default; each LED may
 * independently hold a web-controlled custom color/blink.
 *
 * The state machine is pure logic: it never touches hardware. A sink callback
 * (see led_rgb_set_sink()) receives the resolved frame for the whole chain on
 * every render, allowing the RMT driver or a test double to consume it.
 *
 * All public functions are safe to call concurrently from different tasks and
 * are internally serialized by a recursive mutex (a no-op shim on the host).
 */
#pragma once

#include "sdkconfig.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Compile-time configuration. When built as part of the application these come
 * from Kconfig; the fallbacks keep the component self-contained for host tests.
 */
#ifdef CONFIG_APP_LED_COUNT
#define LED_RGB_COUNT CONFIG_APP_LED_COUNT
#else
#define LED_RGB_COUNT 1
#endif

#ifdef CONFIG_APP_LED_MAX_BLINK_MS
#define LED_RGB_MAX_BLINK_MS CONFIG_APP_LED_MAX_BLINK_MS
#else
#define LED_RGB_MAX_BLINK_MS 60000
#endif

#ifdef CONFIG_APP_LED_SLOW_BLINK_MS
#define LED_RGB_SLOW_BLINK_MS CONFIG_APP_LED_SLOW_BLINK_MS
#else
#define LED_RGB_SLOW_BLINK_MS 500
#endif

#ifdef CONFIG_APP_LED_FAST_BLINK_MS
#define LED_RGB_FAST_BLINK_MS CONFIG_APP_LED_FAST_BLINK_MS
#else
#define LED_RGB_FAST_BLINK_MS 100
#endif

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

    /** @brief Resolved output for a single LED at a point in time. */
    typedef struct
    {
        led_rgb_state_t state; /**< State that produced this output. */
        led_rgb_color_t color; /**< Color before the blink on/off is applied. */
        uint32_t blink_ms;     /**< Blink period, 0 means solid. */
        bool on;               /**< Whether the LED is lit at this instant. */
    } led_rgb_output_t;

    /** @brief A custom command targeting one LED. */
    typedef struct
    {
        uint16_t id;           /**< LED ID, 0 .. LED_RGB_COUNT-1. */
        led_rgb_color_t color; /**< Target color. */
        uint32_t blink_ms;     /**< Blink period, 0 means solid. */
    } led_rgb_cmd_t;

    /**
     * @brief Sink callback receiving the resolved frame for the whole chain.
     *
     * @param frame  Resolved outputs, one per LED in ID order.
     * @param count  Number of entries in @p frame (LED_RGB_COUNT).
     */
    typedef void (*led_rgb_sink_t) (const led_rgb_output_t *frame, size_t count);

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
     * @brief Set the automatic state for all LEDs, clearing custom overrides.
     *
     * Out-of-range values and LED_RGB_STATE_CUSTOM are ignored.
     *
     * @param state  Target automatic state.
     */
    void led_rgb_set_state (led_rgb_state_t state);

    /**
     * @brief Get the state of one LED (LED_RGB_STATE_CUSTOM if overridden).
     *
     * @param id  LED ID.
     * @return The LED state, or LED_RGB_STATE_ERROR if the id is invalid.
     */
    led_rgb_state_t led_rgb_get_state (uint16_t id);

    /**
     * @brief Get the number of LEDs in the chain.
     *
     * @return LED_RGB_COUNT.
     */
    size_t led_rgb_get_count (void);

    /**
     * @brief Set one LED to a custom color and blink rate.
     *
     * @return ESP_OK, ESP_ERR_INVALID_ARG (bad id/blink) or
     *         ESP_ERR_INVALID_STATE (not connected).
     */
    esp_err_t led_rgb_set_custom (uint16_t id, led_rgb_color_t color, uint32_t blink_ms);

    /**
     * @brief Set several LEDs from an array of commands (atomic, all-or-nothing).
     *
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_STATE.
     */
    esp_err_t led_rgb_set_custom_many (const led_rgb_cmd_t *cmds, size_t count);

    /**
     * @brief Apply the same custom color/blink to every LED (atomic).
     *
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_STATE.
     */
    esp_err_t led_rgb_set_custom_all (led_rgb_color_t color, uint32_t blink_ms);

    /**
     * @brief Clear the custom override of one LED.
     *
     * @return ESP_OK, or ESP_ERR_INVALID_ARG if the id is invalid.
     */
    esp_err_t led_rgb_clear_custom (uint16_t id);

    /**
     * @brief Clear the custom overrides of the listed LEDs (atomic).
     *
     * @return ESP_OK, or ESP_ERR_INVALID_ARG if any id is invalid.
     */
    esp_err_t led_rgb_clear_custom_many (const uint16_t *ids, size_t count);

    /**
     * @brief Clear the custom override of every LED.
     *
     * @return ESP_OK.
     */
    esp_err_t led_rgb_clear_custom_all (void);

    /**
     * @brief Check whether a LED is in the custom state.
     *
     * @param id  LED ID.
     * @return true if the LED has a custom override.
     */
    bool led_rgb_is_custom (uint16_t id);

    /**
     * @brief Resolve one LED's output for a given time (no sink call).
     *
     * @param id      LED ID.
     * @param now_ms  Monotonic time in milliseconds.
     * @param out     Destination; must not be NULL.
     */
    void led_rgb_get_output (uint16_t id, uint32_t now_ms, led_rgb_output_t *out);

    /**
     * @brief Resolve the whole chain and forward it to the registered sink.
     *
     * @param now_ms  Monotonic time in milliseconds.
     */
    void led_rgb_render (uint32_t now_ms);

#ifdef __cplusplus
}
#endif
