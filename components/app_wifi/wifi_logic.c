#include "wifi_logic.h"

#include <stddef.h>

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
