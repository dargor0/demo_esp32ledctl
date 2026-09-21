/**
 * @file wifi_logic.c
 * @brief Pure WiFi connection policy: state transitions and reconnect backoff.
 *
 * ALGORITHM
 * ---------
 * The policy maps app-level events to an application state and, on a disconnect,
 * decides how long to wait before reconnecting:
 *
 *   CONNECT_REQUESTED / STA_STARTED -> CONNECTING
 *   GOT_IP                          -> CONNECTED (and reset the retry counter)
 *   DISCONNECTED                    -> CONNECTING with a backoff delay, or
 *                                      ERROR once max_retries is reached
 *   FAILED                          -> ERROR
 *
 * Backoff is exponential: delay = base_delay * 2^retries, capped at max_delay.
 * The retry counter is incremented on each disconnect and reset on GOT_IP, so a
 * successful connection restarts the schedule. max_retries == 0 means unlimited.
 *
 * No hardware/FreeRTOS dependency, so it is host-tested.
 */
#include "wifi_logic.h"

#include <stddef.h>

/** @brief Initialize to IDLE with the given (or default) policy parameters. */
void
wifi_logic_init (wifi_logic_t *logic, const wifi_logic_config_t *config)
{
    if (logic == NULL)
        {
            return;
        }

    logic->state = APP_WIFI_STATE_IDLE;
    logic->retries = 0;

    if (config != NULL)
        {
            logic->base_delay_ms = config->base_delay_ms;
            logic->max_delay_ms = config->max_delay_ms;
            logic->max_retries = config->max_retries;
        }
    else
        {
            logic->base_delay_ms = WIFI_LOGIC_DEFAULT_BASE_DELAY_MS;
            logic->max_delay_ms = WIFI_LOGIC_DEFAULT_MAX_DELAY_MS;
            logic->max_retries = 0;
        }
}

/**
 * @brief Compute the delay for the current retry count.
 *
 * Doubles base_delay once per retry, stopping early once the cap is reached to
 * avoid overflow on a large retry count.
 *
 * @param logic  Policy state.
 * @return base_delay * 2^retries, capped at max_delay_ms.
 */
static uint32_t
wifi_logic_backoff (const wifi_logic_t *logic)
{
    uint32_t delay = logic->base_delay_ms;
    for (uint32_t i = 0; i < logic->retries && delay < logic->max_delay_ms; i++)
        {
            delay *= 2;
        }
    return delay > logic->max_delay_ms ? logic->max_delay_ms : delay;
}

/**
 * @brief Handle a disconnect: either schedule a retry or give up.
 *
 * Sequence:
 *   1. If a retry budget is set and already exhausted -> ERROR.
 *   2. Otherwise return the backoff delay for the *current* retry count and then
 *      increment the counter, so the first retry uses base_delay (not 2x).
 *
 * @param logic            Policy state.
 * @param retry_delay_ms   Optional; receives the retry delay.
 * @return The resulting state.
 */
static app_wifi_state_t
wifi_logic_on_disconnected (wifi_logic_t *logic, uint32_t *retry_delay_ms)
{
    if (logic->max_retries > 0 && logic->retries >= logic->max_retries)
        {
            logic->state = APP_WIFI_STATE_ERROR;
            return logic->state;
        }

    if (retry_delay_ms != NULL)
        {
            *retry_delay_ms = wifi_logic_backoff (logic);
        }
    logic->retries++;
    logic->state = APP_WIFI_STATE_CONNECTING;
    return logic->state;
}

/**
 * @brief Apply one event and return the resulting state.
 *
 * retry_delay_ms is always zeroed first; only a DISCONNECTED event sets it.
 */
app_wifi_state_t
wifi_logic_process (wifi_logic_t *logic, wifi_logic_event_t event, uint32_t *retry_delay_ms)
{
    if (logic == NULL)
        {
            return APP_WIFI_STATE_ERROR;
        }
    if (retry_delay_ms != NULL)
        {
            *retry_delay_ms = 0;
        }

    switch (event)
        {
        case WIFI_LOGIC_CONNECT_REQUESTED:
        case WIFI_LOGIC_STA_STARTED:
            logic->state = APP_WIFI_STATE_CONNECTING;
            break;
        case WIFI_LOGIC_GOT_IP:
            /* A successful connection restarts the backoff schedule. */
            logic->retries = 0;
            logic->state = APP_WIFI_STATE_CONNECTED;
            break;
        case WIFI_LOGIC_DISCONNECTED:
            wifi_logic_on_disconnected (logic, retry_delay_ms);
            break;
        case WIFI_LOGIC_FAILED:
            logic->state = APP_WIFI_STATE_ERROR;
            break;
        default:
            break;
        }

    return logic->state;
}
