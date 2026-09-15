/**
 * @file api_json.h
 * @brief Pure JSON parsing/formatting for the REST API.
 *
 * Uses the built-in cJSON component and has no hardware dependency, so it is
 * unit-tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Largest accepted custom blink period in milliseconds. */
#define API_JSON_MAX_BLINK_MS 60000

    /** @brief Parsed LED command from a POST /api/led body. */
    typedef struct
    {
        int r;             /**< Red 0-255. */
        int g;             /**< Green 0-255. */
        int b;             /**< Blue 0-255. */
        uint32_t blink_ms; /**< Blink period; 0 means solid. */
    } api_led_command_t;

    /** @brief Fields of the GET /api/status response. */
    typedef struct
    {
        const char *system; /**< System name. */
        const char *state;  /**< Application state name. */
        const char *wifi;   /**< WiFi state name. */
        const char *ip;     /**< IPv4 address string, or "". */
        uint32_t uptime_s;  /**< Uptime in seconds. */
        uint32_t free_heap; /**< Free heap in bytes. */
    } api_status_t;

    /** @brief Fields of the GET /api/led response. */
    typedef struct
    {
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
     * @brief Parse a POST /api/led JSON body.
     *
     * Expects {"r":0-255,"g":0-255,"b":0-255,"blink_ms":0-60000}; blink_ms is
     * optional and defaults to 0.
     *
     * @param body  NUL-terminated JSON body.
     * @param out   Parsed command; must not be NULL.
     * @return ESP_OK on success, otherwise ESP_ERR_INVALID_ARG.
     */
    esp_err_t api_json_parse_led (const char *body, api_led_command_t *out);

    /**
     * @brief Format the GET /api/status response.
     *
     * @param status    Status fields; must not be NULL.
     * @param out       Destination buffer.
     * @param out_size  Destination size.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t api_json_format_status (const api_status_t *status, char *out, size_t out_size);

    /**
     * @brief Format the GET /api/led response.
     *
     * @param led       LED state; must not be NULL.
     * @param out       Destination buffer.
     * @param out_size  Destination size.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t api_json_format_led (const api_led_state_t *led, char *out, size_t out_size);

    /**
     * @brief Format the GET /api/time response.
     *
     * @param time      Time fields; must not be NULL.
     * @param out       Destination buffer.
     * @param out_size  Destination size.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t api_json_format_time (const api_time_t *time, char *out, size_t out_size);

    /**
     * @brief Format a generic {"ok":..,"message":..} response.
     *
     * @param ok        Success flag.
     * @param message   Optional message; NULL omits the field.
     * @param out       Destination buffer.
     * @param out_size  Destination size.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t api_json_format_ok (bool ok, const char *message, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
