/**
 * @file app_wifi.h
 * @brief WiFi station lifecycle and reconnection.
 *
 * app_wifi owns the station interface: it initializes the network stack, starts
 * WiFi, connects with the supplied credentials, and automatically reconnects on
 * disconnect with an exponential backoff. State transitions are reported
 * through a callback so the LED task and REST API can reflect them.
 *
 * The connection policy (state transitions and backoff) lives in the pure
 * wifi_logic module (see wifi_logic.h) and is unit-tested on the host.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Application-level WiFi state. */
    typedef enum
    {
        APP_WIFI_STATE_IDLE = 0,   /**< Not started / disconnected. */
        APP_WIFI_STATE_CONNECTING, /**< Connecting or reconnecting. */
        APP_WIFI_STATE_CONNECTED,  /**< Station got an IP address. */
        APP_WIFI_STATE_ERROR,      /**< Gave up after repeated failures. */
    } app_wifi_state_t;

    /**
     * @brief State-change callback.
     *
     * @param state      New state.
     * @param user_data  Opaque pointer passed to app_wifi_start().
     */
    typedef void (*app_wifi_state_cb_t) (app_wifi_state_t state, void *user_data);

    /**
     * @brief Initialize the network stack and start WiFi in station mode.
     *
     * @param callback   Called on every state change; may be NULL.
     * @param user_data  Passed back to the callback.
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t app_wifi_start (app_wifi_state_cb_t callback, void *user_data);

    /**
     * @brief Connect to a network, replacing any previous configuration.
     *
     * @param ssid      Network SSID; must not be NULL.
     * @param password  Network password (empty for an open network); not NULL.
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t app_wifi_connect (const char *ssid, const char *password);

    /**
     * @brief Get the current application WiFi state.
     *
     * @return The current state.
     */
    app_wifi_state_t app_wifi_get_state (void);

#ifdef __cplusplus
}
#endif
