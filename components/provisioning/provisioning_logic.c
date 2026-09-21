/**
 * @file provisioning_logic.c
 * @brief Pure provisioning state machine and BLE device-name derivation.
 *
 * STATE MACHINE
 * -------------
 *   init(provisioned):
 *       provisioned  -> IDLE         (nothing to do, connect with stored creds)
 *       !provisioned -> PROVISIONING (wait for credentials over BLE)
 *
 *   STARTED / RESET -> PROVISIONING
 *   CREDENTIALS     -> CONNECTING   (SSID/password received, verifying)
 *   CONNECTED       -> CONNECTED    (manager verified the credentials)
 *   FAILED          -> FAILED
 *
 * NAME DERIVATION
 * ---------------
 * The BLE device/service name is "PROV_" followed by the system name (or the
 * default when empty), truncated to fit the caller's buffer. Bounds are checked
 * so the caller can never see a truncated/overlong string without an error when
 * the buffer is too small to even hold the prefix.
 *
 * No hardware dependency, so it is host-tested.
 */
#include "provisioning_logic.h"

#include <string.h>

/** @brief Initialize to IDLE when already provisioned, else PROVISIONING. */
void
provisioning_logic_init (provisioning_logic_t *logic, bool provisioned)
{
    if (logic == NULL)
        {
            return;
        }
    logic->state = provisioned ? PROV_STATE_IDLE : PROV_STATE_PROVISIONING;
}

/** @brief Map one event to the next state (see the header table). */
provisioning_state_t
provisioning_logic_process (provisioning_logic_t *logic, provisioning_event_t event)
{
    if (logic == NULL)
        {
            return PROV_STATE_FAILED;
        }

    switch (event)
        {
        case PROV_EVENT_STARTED:
        case PROV_EVENT_RESET:
            logic->state = PROV_STATE_PROVISIONING;
            break;
        case PROV_EVENT_CREDENTIALS:
            logic->state = PROV_STATE_CONNECTING;
            break;
        case PROV_EVENT_CONNECTED:
            logic->state = PROV_STATE_CONNECTED;
            break;
        case PROV_EVENT_FAILED:
            logic->state = PROV_STATE_FAILED;
            break;
        default:
            break;
        }

    return logic->state;
}

/**
 * @brief Build "PROV_<system_name>", truncated to the buffer.
 *
 * Sequence:
 *   1. Reject NULL/zero buffers.
 *   2. Require room for the prefix + terminator (otherwise ESP_ERR_INVALID_SIZE).
 *   3. Pick the source (configured name, or the default when empty).
 *   4. Copy the prefix, then the source, stopping before the buffer overflows.
 */
esp_err_t
provisioning_build_name (const char *system_name, char *out, size_t out_size)
{
    static const char prefix[] = "PROV_";

    if (out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    out[0] = '\0';

    if (out_size < sizeof (prefix))
        {
            return ESP_ERR_INVALID_SIZE;
        }

    const char *source = PROVISIONING_DEFAULT_SYSTEM_NAME;
    if (system_name != NULL && system_name[0] != '\0')
        {
            source = system_name;
        }

    size_t length = 0;
    for (size_t i = 0; prefix[i] != '\0' && length < out_size - 1; i++)
        {
            out[length++] = prefix[i];
        }
    for (size_t i = 0; source[i] != '\0' && length < out_size - 1; i++)
        {
            out[length++] = source[i];
        }
    out[length] = '\0';
    return ESP_OK;
}
