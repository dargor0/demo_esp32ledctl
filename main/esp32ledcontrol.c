#include <string.h>

#include "app_wifi.h"
#include "button.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "http_server.h"
#include "led_driver.h"
#include "led_rgb.h"
#include "mdns_service.h"
#include "provisioning.h"
#include "storage.h"
#include "time_sync.h"

static const char *TAG = "app";

#define NET_CONNECTED_BIT BIT0
#define NET_ERROR_BIT BIT1
#define NET_START_PROV_BIT BIT2

static EventGroupHandle_t s_net_events;

/**
 * @brief Map an app state change to the LED state machine.
 *
 * @param state  New application state.
 */
static void
app_wifi_state_cb (app_wifi_state_t state, void *user_data)
{
    (void)user_data;
    if (state == APP_WIFI_STATE_CONNECTED)
        {
            led_rgb_set_state (LED_RGB_STATE_CONNECTED);
            xEventGroupSetBits (s_net_events, NET_CONNECTED_BIT);
        }
    else if (state == APP_WIFI_STATE_ERROR)
        {
            led_rgb_set_state (LED_RGB_STATE_ERROR);
            xEventGroupSetBits (s_net_events, NET_ERROR_BIT);
        }
}

/**
 * @brief Map a provisioning state change to the LED state machine.
 *
 * @param state  New provisioning state.
 */
static void
app_prov_state_cb (provisioning_state_t state, void *user_data)
{
    (void)user_data;
    switch (state)
        {
        case PROV_STATE_PROVISIONING:
            led_rgb_set_state (LED_RGB_STATE_PROVISIONING);
            break;
        case PROV_STATE_CONNECTING:
            led_rgb_set_state (LED_RGB_STATE_CONNECTING);
            break;
        case PROV_STATE_CONNECTED:
            led_rgb_set_state (LED_RGB_STATE_CONNECTED);
            break;
        case PROV_STATE_FAILED:
            led_rgb_set_state (LED_RGB_STATE_ERROR);
            break;
        default:
            break;
        }
}

/**
 * @brief Handle BOOT button events.
 *
 * @param event  Button event.
 */
static void
app_button_cb (button_event_t event, void *user_data)
{
    (void)user_data;
    if (event == BUTTON_EVENT_SHORT_PRESS)
        {
            led_rgb_clear_custom ();
        }
    else if (event == BUTTON_EVENT_LONG_PRESS)
        {
            ESP_LOGW (TAG, "factory reset: erasing credentials");
            storage_erase_wifi ();
            esp_restart ();
        }
}

/**
 * @brief LED task: render the state machine every tick on its core.
 */
static void
app_led_task (void *arg)
{
    (void)arg;
    for (;;)
        {
            led_rgb_render ((uint32_t)(esp_timer_get_time () / 1000));
            vTaskDelay (pdMS_TO_TICKS (CONFIG_APP_LED_TICK_MS));
        }
}

/**
 * @brief Provisioning task: start BLE provisioning when asked by net_task.
 */
static void
app_prov_task (void *arg)
{
    (void)arg;
    xEventGroupWaitBits (s_net_events, NET_START_PROV_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI (TAG, "starting BLE provisioning");
    provisioning_start (CONFIG_APP_SYSTEM_NAME, app_prov_state_cb, NULL);
    for (;;)
        {
            vTaskDelay (pdMS_TO_TICKS (1000));
        }
}

/**
 * @brief Network task: bring up WiFi, connect or provision, then services.
 */
static void
app_net_task (void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK (app_wifi_start (app_wifi_state_cb, NULL));

    storage_wifi_creds_t creds;
    if (storage_has_wifi () && storage_load_wifi (&creds) == ESP_OK)
        {
            led_rgb_set_state (LED_RGB_STATE_CONNECTING);
            ESP_LOGI (TAG, "connecting to '%s'", creds.ssid);
            app_wifi_connect (creds.ssid, creds.password);
        }
    else
        {
            led_rgb_set_state (LED_RGB_STATE_PROVISIONING);
            xEventGroupSetBits (s_net_events, NET_START_PROV_BIT);
        }

    EventBits_t bits = xEventGroupWaitBits (s_net_events, NET_CONNECTED_BIT | NET_ERROR_BIT,
                                            pdFALSE, pdFALSE, portMAX_DELAY);
    if ((bits & NET_CONNECTED_BIT) != 0)
        {
            ESP_LOGI (TAG, "network connected; starting services");
            time_sync_start (CONFIG_APP_NTP_SERVER, CONFIG_APP_TIMEZONE);
            mdns_service_start (CONFIG_APP_SYSTEM_NAME, CONFIG_APP_HTTP_PORT);
            http_server_start (CONFIG_APP_HTTP_PORT, CONFIG_APP_SYSTEM_NAME);
        }

    for (;;)
        {
            vTaskDelay (pdMS_TO_TICKS (1000));
        }
}

void
app_main (void)
{
    s_net_events = xEventGroupCreate ();
    ESP_ERROR_CHECK (s_net_events != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK (storage_init ());
    ESP_ERROR_CHECK (led_driver_init (CONFIG_APP_RGB_GPIO));
    led_rgb_set_state (LED_RGB_STATE_PROVISIONING);

    const button_config_t button_config = {
        .gpio_num = CONFIG_APP_BOOT_GPIO,
        .debounce_ms = CONFIG_APP_BUTTON_DEBOUNCE_MS,
        .long_press_ms = CONFIG_APP_BUTTON_LONGPRESS_MS,
    };
    ESP_ERROR_CHECK (button_start (&button_config, app_button_cb, NULL));

    xTaskCreatePinnedToCore (app_led_task, "led", 3072, NULL, 5, NULL, CONFIG_APP_TASK_CORE_LED);
    xTaskCreatePinnedToCore (app_net_task, "net", 4096, NULL, 6, NULL, CONFIG_APP_TASK_CORE_NET);
    xTaskCreatePinnedToCore (app_prov_task, "prov", 4096, NULL, 6, NULL, CONFIG_APP_TASK_CORE_PROV);

    ESP_LOGI (TAG, "%s ready", CONFIG_APP_SYSTEM_NAME);
}
