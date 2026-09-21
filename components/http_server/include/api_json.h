/**
 * @file api_json.h
 * @brief Pure JSON parsing/formatting for the REST API.
 *
 * Uses the built-in cJSON component and has no hardware dependency, so it is
 * unit-tested on the host.
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

/** @brief Largest accepted custom blink period in milliseconds. */
#ifndef CONFIG_APP_LED_MAX_BLINK_MS
#define API_JSON_MAX_BLINK_MS 60000
#else
#define API_JSON_MAX_BLINK_MS CONFIG_APP_LED_MAX_BLINK_MS
#endif

/** @brief Largest accepted number of LEDs in a bulk command. */
#define API_JSON_MAX_LEDS 1024
/** @brief LED channel sentinel value for "no update" in bulk commands. */
#define API_JSON_CHANNEL_UNSET (-1)

    /** @brief A command for one LED without an id (body of PUT /api/led/{id}). */
    typedef struct
    {
        int r;             /**< Red 0-255. */
        int g;             /**< Green 0-255. */
        int b;             /**< Blue 0-255. */
        uint32_t blink_ms; /**< Blink period; 0 means solid. */
    } api_led_command_t;

    /** @brief A command for one LED with its id (elements of a bulk array). */
    typedef struct
    {
        uint16_t id;       /**< LED id. */
        int r;             /**< Red 0-255. */
        int g;             /**< Green 0-255. */
        int b;             /**< Blue 0-255. */
        uint32_t blink_ms; /**< Blink period; 0 means solid. */
    } api_led_entry_t;

    /** @brief Fields of the GET /api/status response. */
    typedef struct
    {
        const char *system;            /**< System name. */
        const char *state;             /**< Application state name. */
        const char *wifi;              /**< WiFi state name. */
        const char *ip;                /**< IPv4 address string, or "". */
        uint32_t uptime_s;             /**< Uptime in seconds. */
        uint32_t free_heap;            /**< Free heap in bytes. */
        uint32_t led_count;            /**< Number of LEDs in the chain. */
        const char *last_button_event; /**< Last BOOT event name. */
    } api_status_t;

    /** @brief One entry of the GET /api/led response. */
    typedef struct
    {
        uint16_t id;       /**< LED id. */
        const char *state; /**< LED state name. */
        int r;             /**< Red 0-255. */
        int g;             /**< Green 0-255. */
        int b;             /**< Blue 0-255. */
        uint32_t blink_ms; /**< Blink period; 0 means solid. */
        bool on;           /**< Whether the LED is lit right now. */
    } api_led_state_t;

    /** @brief Fields of the GET /api/time response. */
    typedef struct
    {
        bool synced;         /**< Whether the clock is synchronized. */
        int64_t epoch;       /**< Seconds since the Unix epoch. */
        const char *iso8601; /**< ISO-8601 string, or "". */
    } api_time_t;

    /**
     * @brief Check whether a body is a JSON array (leading '[' after whitespace).
     *
     * @param body  NUL-terminated body.
     * @return true if the body looks like an array.
     */
    bool api_json_body_is_array (const char *body);

    /**
     * @brief Parse a single LED command (object without `id`).
     *
     * Expects {"r":0-255,"g":0-255,"b":0-255,"blink_ms":0-..}; blink_ms optional
     * and defaults to 0. An `id` member is rejected.
     *
     * @param body  NUL-terminated JSON body.
     * @param out   Parsed command; must not be NULL.
     * @return ESP_OK, or ESP_ERR_INVALID_ARG.
     */
    esp_err_t api_json_parse_led (const char *body, api_led_command_t *out);

    /**
     * @brief Parse an array of per-LED commands.
     *
     * Expects [{"id":K,"r":..,"g":..,"b":..,"blink_ms":..}, ...].
     *
     * @param body         NUL-terminated JSON body.
     * @param out          Destination array.
     * @param max_entries  Capacity of @p out.
     * @param out_count    Receives the number of parsed entries.
     * @return ESP_OK, or ESP_ERR_INVALID_ARG.
     */
    esp_err_t api_json_parse_led_many (const char *body, api_led_entry_t *out, size_t max_entries,
                                       size_t *out_count);

    /**
     * @brief Parse an optional {"ids":[K,...]} body into an id list.
     *
     * A NULL body or an object without `ids` yields an empty list.
     *
     * @param body         NUL-terminated JSON body, or NULL.
     * @param out          Destination array.
     * @param max_entries  Capacity of @p out.
     * @param out_count    Receives the number of parsed ids.
     * @return ESP_OK, or ESP_ERR_INVALID_ARG.
     */
    esp_err_t api_json_parse_ids (const char *body, uint16_t *out, size_t max_entries,
                                  size_t *out_count);

    /**
     * @brief Format a single LED object (GET /api/led/{id}).
     */
    esp_err_t api_json_format_led (const api_led_state_t *led, char *out, size_t out_size);

    /**
     * @brief Format a LED collection (GET /api/led).
     */
    esp_err_t api_json_format_led_list (const api_led_state_t *leds, size_t count, char *out,
                                        size_t out_size);

    /**
     * @brief Format the GET /api/status response.
     */
    esp_err_t api_json_format_status (const api_status_t *status, char *out, size_t out_size);

    /**
     * @brief Format the GET /api/time response.
     */
    esp_err_t api_json_format_time (const api_time_t *time, char *out, size_t out_size);

    /**
     * @brief Format a generic {"ok":..,"message":..} response.
     */
    esp_err_t api_json_format_ok (bool ok, const char *message, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
