/**
 * @file provisioning.h
 * @brief BLE provisioning of WiFi credentials.
 *
 * The provisioning component captures WiFi credentials over BLE using the
 * espressif/network_provisioning manager (BLE scheme), stores them through the
 * storage component, and reports progress through a state callback.
 *
 * Contract: the network stack must already be initialized (for example by
 * app_wifi_start()) before provisioning_start() is called, so that WiFi is not
 * initialized twice.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief System name used when none is configured. */
#define PROVISIONING_DEFAULT_SYSTEM_NAME "esp32ledctl"
/** @brief Maximum length of the BLE device/service name, including terminator. */
#define PROVISIONING_MAX_NAME_LEN 32
/** @brief Proof-of-possession used by the MVP security scheme. */
#define PROVISIONING_POP "abcd1234"

    /** @brief Provisioning lifecycle state. */
    typedef enum
    {
        PROV_STATE_IDLE = 0,     /**< Already provisioned; nothing to do. */
        PROV_STATE_PROVISIONING, /**< Advertising and waiting for credentials. */
        PROV_STATE_CONNECTING,   /**< Credentials received; connecting. */
        PROV_STATE_CONNECTED,    /**< Provisioning succeeded. */
        PROV_STATE_FAILED,       /**< Provisioning failed. */
    } provisioning_state_t;

    /**
     * @brief Provisioning state-change callback.
     *
     * @param state      New state.
     * @param user_data  Opaque pointer passed to provisioning_start().
     */
    typedef void (*provisioning_state_cb_t) (provisioning_state_t state, void *user_data);

    /**
     * @brief Check whether credentials are already stored.
     *
     * @return true when the device is provisioned.
     */
    bool provisioning_is_provisioned (void);

    /**
     * @brief Start BLE provisioning when no credentials are stored.
     *
     * If credentials are already stored, the callback is invoked with
     * PROV_STATE_IDLE and no provisioning service is started.
     *
     * @param system_name  System name used to derive the BLE device name.
     * @param callback     Called on every state change; may be NULL.
     * @param user_data    Passed back to the callback.
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t provisioning_start (const char *system_name, provisioning_state_cb_t callback,
                                  void *user_data);

    /**
     * @brief Erase stored credentials and reset provisioning.
     *
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t provisioning_reset (void);

#ifdef __cplusplus
}
#endif
