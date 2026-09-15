#include "provisioning_logic.h"

#include <string.h>

void
provisioning_logic_init (provisioning_logic_t *logic, bool provisioned)
{
    if (logic == NULL)
        {
            return;
        }
    logic->state = provisioned ? PROV_STATE_IDLE : PROV_STATE_PROVISIONING;
}

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
