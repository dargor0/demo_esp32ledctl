/**
 * @file provisioning.c
 * @brief BLE provisioning glue over espressif/network_provisioning.
 *
 * SEQUENCE
 * --------
 *   provisioning_start(system_name, cb, ud):
 *     1. read whether credentials already exist (storage);
 *     2. init the state machine (IDLE if provisioned, else PROVISIONING) and
 *        publish the initial state;
 *     3. if already provisioned/started, stop here;
 *     4. register the NETWORK_PROV_EVENT handler, init the manager with the BLE
 *        scheme, build the "PROV_<name>" device name and start provisioning.
 *
 *   Event handler:
 *     START      -> publish STARTED;
 *     CRED_RECV  -> save SSID/password to NVS, publish CREDENTIALS;
 *     CRED_SUCCESS -> publish CONNECTED;
 *     CRED_FAIL  -> publish FAILED and reset the manager state machine;
 *     END        -> deinit the manager.
 *
 * The mutex guards the state machine/callback/started; the manager calls and the
 * user callback run outside it. In static-credentials mode this file is not
 * compiled at all (see CMakeLists / FR-11).
 */
#include "provisioning.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "provisioning_logic.h"
#include "sdkconfig.h"
#include "storage.h"

#ifndef CONFIG_APP_PROV_POP
#define CONFIG_APP_PROV_POP "abcd1234"
#endif

static const char *TAG = "provisioning";

static provisioning_logic_t s_logic;
static provisioning_state_cb_t s_callback;
static void *s_user_data;
static bool s_started;
static SemaphoreHandle_t s_lock;

/** @brief Create the recursive mutex on first use. */
static void
prov_lock_init (void)
{
    if (s_lock == NULL)
        {
            s_lock = xSemaphoreCreateRecursiveMutex ();
        }
}

static void
prov_lock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreTakeRecursive (s_lock, portMAX_DELAY);
        }
}

static void
prov_unlock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreGiveRecursive (s_lock);
        }
}

/**
 * @brief Apply a provisioning event and publish the resulting state.
 *
 * The callback runs after releasing the lock (FR-9).
 *
 * @param event  Event to apply.
 */
static void
provisioning_publish (provisioning_event_t event)
{
    prov_lock ();
    provisioning_state_t state = provisioning_logic_process (&s_logic, event);
    provisioning_state_cb_t callback = s_callback;
    void *user_data = s_user_data;
    prov_unlock ();

    ESP_LOGI (TAG, "state -> %d", (int)state);
    if (callback != NULL)
        {
            callback (state, user_data);
        }
}

/**
 * @brief Persist credentials received during provisioning.
 *
 * @param config  WiFi station configuration delivered by the manager.
 */
static void
provisioning_store_credentials (const wifi_sta_config_t *config)
{
    storage_wifi_creds_t creds = { 0 };
    strncpy (creds.ssid, (const char *)config->ssid, STORAGE_SSID_MAX_LEN);
    strncpy (creds.password, (const char *)config->password, STORAGE_PASSWORD_MAX_LEN);

    esp_err_t err = storage_save_wifi (&creds);
    if (err != ESP_OK)
        {
            ESP_LOGW (TAG, "failed to store credentials: %s", esp_err_to_name (err));
        }
}

/**
 * @brief network_prov_mgr event handler.
 */
static void
provisioning_event_handler (void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != NETWORK_PROV_EVENT)
        {
            return;
        }

    switch (id)
        {
        case NETWORK_PROV_START:
            provisioning_publish (PROV_EVENT_STARTED);
            break;
        case NETWORK_PROV_WIFI_CRED_RECV:
            provisioning_store_credentials ((const wifi_sta_config_t *)data);
            provisioning_publish (PROV_EVENT_CREDENTIALS);
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            provisioning_publish (PROV_EVENT_CONNECTED);
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL:
            provisioning_publish (PROV_EVENT_FAILED);
            network_prov_mgr_reset_wifi_sm_state_on_failure ();
            break;
        case NETWORK_PROV_END:
            network_prov_mgr_deinit ();
            break;
        default:
            break;
        }
}

bool
provisioning_is_provisioned (void)
{
    return storage_has_wifi ();
}

esp_err_t
provisioning_start (const char *system_name, provisioning_state_cb_t callback, void *user_data)
{
    prov_lock_init ();
    bool provisioned = provisioning_is_provisioned ();

    prov_lock ();
    s_callback = callback;
    s_user_data = user_data;
    provisioning_logic_init (&s_logic, provisioned);
    provisioning_state_t initial = s_logic.state;
    bool already_started = s_started;
    prov_unlock ();

    if (callback != NULL)
        {
            callback (initial, user_data);
        }
    if (provisioned || already_started)
        {
            return ESP_OK;
        }

    ESP_RETURN_ON_ERROR (esp_event_handler_instance_register (NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                              &provisioning_event_handler, NULL,
                                                              NULL),
                         TAG, "register provisioning handler");

    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_RETURN_ON_ERROR (network_prov_mgr_init (config), TAG, "init provisioning manager");

    char service_name[PROVISIONING_MAX_NAME_LEN];
    ESP_RETURN_ON_ERROR (provisioning_build_name (system_name, service_name, sizeof (service_name)),
                         TAG, "build device name");

    esp_err_t err = network_prov_mgr_start_provisioning (NETWORK_PROV_SECURITY_1,
                                                         CONFIG_APP_PROV_POP, service_name, NULL);
    if (err != ESP_OK)
        {
            network_prov_mgr_deinit ();
            return err;
        }

    prov_lock ();
    s_started = true;
    prov_unlock ();
    return ESP_OK;
}

esp_err_t
provisioning_reset (void)
{
    ESP_RETURN_ON_ERROR (storage_erase_wifi (), TAG, "erase credentials");
    ESP_RETURN_ON_ERROR (network_prov_mgr_reset_wifi_provisioning (), TAG, "reset provisioning");

    prov_lock ();
    provisioning_logic_init (&s_logic, false);
    prov_unlock ();
    return ESP_OK;
}
