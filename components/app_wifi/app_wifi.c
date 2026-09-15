#include "app_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "wifi_logic.h"

static const char *TAG = "app_wifi";

static app_wifi_state_t s_state = APP_WIFI_STATE_IDLE;
static app_wifi_state_cb_t s_callback;
static void *s_user_data;
static wifi_logic_t s_logic;
static esp_timer_handle_t s_reconnect_timer;
static bool s_started;

/**
 * @brief Publish a state change to the callback.
 *
 * @param state  New state.
 */
static void
app_wifi_set_state (app_wifi_state_t state)
{
    if (state == s_state)
        {
            return;
        }
    s_state = state;
    ESP_LOGI (TAG, "state -> %d", (int)state);
    if (s_callback != NULL)
        {
            s_callback (state, s_user_data);
        }
}

/**
 * @brief Run the connection policy and act on the result.
 *
 * @param event  Policy event to apply.
 */
static void
app_wifi_apply (wifi_logic_event_t event)
{
    uint32_t delay_ms = 0;
    app_wifi_state_t state = wifi_logic_process (&s_logic, event, &delay_ms);
    app_wifi_set_state (state);

    if (event != WIFI_LOGIC_DISCONNECTED || state != APP_WIFI_STATE_CONNECTING)
        {
            return;
        }
    if (delay_ms == 0)
        {
            esp_wifi_connect ();
        }
    else
        {
            esp_timer_start_once (s_reconnect_timer, (uint64_t)delay_ms * 1000);
        }
}

/**
 * @brief Reconnect timer callback.
 *
 * @param arg  Unused.
 */
static void
app_wifi_reconnect_timer_cb (void *arg)
{
    (void)arg;
    esp_wifi_connect ();
}

/**
 * @brief WiFi event handler.
 */
static void
app_wifi_wifi_event_handler (void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START)
        {
            app_wifi_apply (WIFI_LOGIC_STA_STARTED);
        }
    else if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            app_wifi_apply (WIFI_LOGIC_DISCONNECTED);
        }
}

/**
 * @brief IP event handler.
 */
static void
app_wifi_ip_event_handler (void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == IP_EVENT_STA_GOT_IP)
        {
            app_wifi_apply (WIFI_LOGIC_GOT_IP);
        }
}

esp_err_t
app_wifi_start (app_wifi_state_cb_t callback, void *user_data)
{
    if (s_started)
        {
            return ESP_OK;
        }

    s_callback = callback;
    s_user_data = user_data;
    wifi_logic_init (&s_logic, NULL);

    ESP_RETURN_ON_ERROR (esp_netif_init (), TAG, "init netif");
    esp_err_t err = esp_event_loop_create_default ();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            return err;
        }
    if (esp_netif_create_default_wifi_sta () == NULL)
        {
            return ESP_FAIL;
        }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT ();
    ESP_RETURN_ON_ERROR (esp_wifi_init (&init_config), TAG, "init wifi");

    ESP_RETURN_ON_ERROR (esp_event_handler_instance_register (WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                              &app_wifi_wifi_event_handler, NULL,
                                                              NULL),
                         TAG, "register wifi handler");
    ESP_RETURN_ON_ERROR (esp_event_handler_instance_register (
                             IP_EVENT, IP_EVENT_STA_GOT_IP, &app_wifi_ip_event_handler, NULL, NULL),
                         TAG, "register ip handler");

    const esp_timer_create_args_t timer_args = {
        .callback = app_wifi_reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_RETURN_ON_ERROR (esp_timer_create (&timer_args, &s_reconnect_timer), TAG, "create timer");

    ESP_RETURN_ON_ERROR (esp_wifi_set_mode (WIFI_MODE_STA), TAG, "set mode");
    ESP_RETURN_ON_ERROR (esp_wifi_start (), TAG, "start wifi");

    s_started = true;
    return ESP_OK;
}

esp_err_t
app_wifi_connect (const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }
    if (!s_started)
        {
            return ESP_ERR_INVALID_STATE;
        }

    wifi_config_t wifi_config = { 0 };
    strncpy ((char *)wifi_config.sta.ssid, ssid, sizeof (wifi_config.sta.ssid) - 1);
    strncpy ((char *)wifi_config.sta.password, password, sizeof (wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR (esp_wifi_set_config (WIFI_IF_STA, &wifi_config), TAG, "set config");
    app_wifi_apply (WIFI_LOGIC_CONNECT_REQUESTED);
    return esp_wifi_connect ();
}

app_wifi_state_t
app_wifi_get_state (void)
{
    return s_state;
}
