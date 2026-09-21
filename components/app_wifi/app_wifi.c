/**
 * @file app_wifi.c
 * @brief ESP WiFi station glue: event handling + policy application.
 *
 * SEQUENCE
 * --------
 *   app_wifi_start(): netif/event-loop/WiFi init, register WIFI_EVENT and
 *     IP_EVENT handlers, start the station, then set started=true.
 *   app_wifi_connect(ssid,pass): store the station config and kick off a
 *     connection (CONNECT_REQUESTED).
 *   Event handler -> app_wifi_apply(event):
 *       1. run the pure policy under the lock (state + retry delay);
 *       2. release the lock, then log/invoke the state callback;
 *       3. for a disconnect that should retry, either reconnect immediately
 *          (delay 0) or arm the one-shot reconnect timer.
 *
 * The lock protects s_state/s_logic/s_started; blocking actions and the user
 * callback run outside it (FR-9).
 */
#include "app_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "wifi_logic.h"

static const char *TAG = "app_wifi";

static app_wifi_state_t s_state = APP_WIFI_STATE_IDLE;
static app_wifi_state_cb_t s_callback;
static void *s_user_data;
static wifi_logic_t s_logic;
static esp_timer_handle_t s_reconnect_timer;
static bool s_started;
static SemaphoreHandle_t s_lock;

/** @brief Create the recursive mutex on first use. */
static void
app_wifi_lock_init (void)
{
    if (s_lock == NULL)
        {
            s_lock = xSemaphoreCreateRecursiveMutex ();
        }
}

static void
app_wifi_lock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreTakeRecursive (s_lock, portMAX_DELAY);
        }
}

static void
app_wifi_unlock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreGiveRecursive (s_lock);
        }
}

/**
 * @brief Run the connection policy and act on the result.
 *
 * The state is updated under the lock; the callback and the blocking actions
 * (esp_wifi_connect / reconnect timer) run after releasing it (FR-9).
 *
 * @param event  Policy event to apply.
 */
static void
app_wifi_apply (wifi_logic_event_t event)
{
    uint32_t delay_ms = 0;

    app_wifi_lock ();
    app_wifi_state_t previous = s_state;
    app_wifi_state_t state = wifi_logic_process (&s_logic, event, &delay_ms);
    s_state = state;
    app_wifi_unlock ();

    if (state != previous)
        {
            ESP_LOGI (TAG, "state -> %d", (int)state);
            if (s_callback != NULL)
                {
                    s_callback (state, s_user_data);
                }
        }

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
    app_wifi_lock_init ();
    app_wifi_lock ();
    if (s_started)
        {
            app_wifi_unlock ();
            return ESP_OK;
        }
    s_callback = callback;
    s_user_data = user_data;
    wifi_logic_init (&s_logic, NULL);
    app_wifi_unlock ();

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

    app_wifi_lock ();
    s_started = true;
    app_wifi_unlock ();
    return ESP_OK;
}

esp_err_t
app_wifi_connect (const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    app_wifi_lock ();
    bool started = s_started;
    app_wifi_unlock ();
    if (!started)
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
    app_wifi_lock ();
    app_wifi_state_t state = s_state;
    app_wifi_unlock ();
    return state;
}
