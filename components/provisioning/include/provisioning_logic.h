/**
 * @file provisioning_logic.h
 * @brief Pure provisioning state machine and BLE device-name derivation.
 *
 * No ESP-IDF dependency beyond esp_err_t. Unit-tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "provisioning.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Events driving the provisioning state machine. */
    typedef enum
    {
        PROV_EVENT_STARTED = 0, /**< The provisioning service started. */
        PROV_EVENT_CREDENTIALS, /**< Credentials were received. */
        PROV_EVENT_CONNECTED,   /**< The network connection succeeded. */
        PROV_EVENT_FAILED,      /**< Provisioning failed. */
        PROV_EVENT_RESET,       /**< Provisioning was reset. */
    } provisioning_event_t;

    /** @brief Provisioning state machine. */
    typedef struct
    {
        provisioning_state_t state; /**< Current state. */
    } provisioning_logic_t;

    /**
     * @brief Initialize the state machine.
     *
     * @param logic       State machine; must not be NULL.
     * @param provisioned Whether credentials are already stored.
     */
    void provisioning_logic_init (provisioning_logic_t *logic, bool provisioned);

    /**
     * @brief Apply an event and return the resulting state.
     *
     * @param logic  State machine; must not be NULL.
     * @param event  Event to apply.
     * @return The resulting state.
     */
    provisioning_state_t provisioning_logic_process (provisioning_logic_t *logic,
                                                     provisioning_event_t event);

    /**
     * @brief Build the BLE device/service name from the system name.
     *
     * The result is "PROV_<system_name>", truncated to fit out_size. An empty or
     * NULL system name selects PROVISIONING_DEFAULT_SYSTEM_NAME.
     *
     * @param system_name  Configured system name, may be NULL or empty.
     * @param out          Destination buffer; must not be NULL.
     * @param out_size     Destination size; must be > 0.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t provisioning_build_name (const char *system_name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
