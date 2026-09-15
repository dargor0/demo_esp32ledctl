/**
 * @file wifi_logic.h
 * @brief Pure WiFi connection state and backoff policy.
 *
 * No hardware or ESP-IDF dependency: it maps app-level events to states and
 * computes the reconnection delay. Unit-tested on the host.
 */
#pragma once

#include <stdint.h>

#include "app_wifi.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Default first reconnection delay in milliseconds. */
#define WIFI_LOGIC_DEFAULT_BASE_DELAY_MS 1000
/** @brief Default maximum reconnection delay in milliseconds. */
#define WIFI_LOGIC_DEFAULT_MAX_DELAY_MS 30000

    /** @brief Events driving the connection policy. */
    typedef enum
    {
        WIFI_LOGIC_CONNECT_REQUESTED = 0, /**< A connection was requested. */
        WIFI_LOGIC_STA_STARTED,           /**< The station driver started. */
        WIFI_LOGIC_GOT_IP,                /**< An IP address was obtained. */
        WIFI_LOGIC_DISCONNECTED,          /**< The station disconnected. */
        WIFI_LOGIC_FAILED,                /**< An unrecoverable failure occurred. */
    } wifi_logic_event_t;

    /** @brief Connection policy configuration. */
    typedef struct
    {
        uint32_t base_delay_ms; /**< First retry delay. */
        uint32_t max_delay_ms;  /**< Retry delay ceiling. */
        uint32_t max_retries;   /**< Retries before ERROR; 0 means unlimited. */
    } wifi_logic_config_t;

    /** @brief Connection policy state. */
    typedef struct
    {
        app_wifi_state_t state; /**< Current application state. */
        uint32_t retries;       /**< Consecutive reconnect attempts. */
        uint32_t base_delay_ms; /**< First retry delay. */
        uint32_t max_delay_ms;  /**< Retry delay ceiling. */
        uint32_t max_retries;   /**< Retries before ERROR; 0 means unlimited. */
    } wifi_logic_t;

    /**
     * @brief Initialize the connection policy.
     *
     * @param logic   Policy state; must not be NULL.
     * @param config  Configuration, or NULL for the defaults.
     */
    void wifi_logic_init (wifi_logic_t *logic, const wifi_logic_config_t *config);

    /**
     * @brief Apply an event and return the resulting state.
     *
     * @param logic           Policy state; must not be NULL.
     * @param event           Event to apply.
     * @param retry_delay_ms  Optional; receives the reconnection delay for
     *                        WIFI_LOGIC_DISCONNECTED (0 otherwise).
     * @return The resulting application state.
     */
    app_wifi_state_t wifi_logic_process (wifi_logic_t *logic, wifi_logic_event_t event,
                                         uint32_t *retry_delay_ms);

#ifdef __cplusplus
}
#endif
