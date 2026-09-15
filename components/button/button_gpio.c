#include "button.h"

#include <string.h>

#include "button_sm.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** @brief GPIO polling period in milliseconds. */
#define BUTTON_POLL_INTERVAL_MS 10
/** @brief Stack size for the button task. */
#define BUTTON_TASK_STACK_SIZE 3072
/** @brief Priority for the button task. */
#define BUTTON_TASK_PRIORITY 5

static const char *TAG = "button";

/** @brief Module context shared with the button task. */
typedef struct
{
    button_config_t config;
    button_event_cb_t callback;
    void *user_data;
} button_context_t;

static button_context_t s_context;

/**
 * @brief Poll the GPIO, run the state machine and dispatch events.
 *
 * @param arg  Unused.
 */
static void
button_task (void *arg)
{
    (void)arg;
    button_sm_t sm;
    button_sm_init (&sm, s_context.config.debounce_ms, s_context.config.long_press_ms);

    for (;;)
        {
            bool raw_pressed = gpio_get_level (s_context.config.gpio_num) == 0;
            uint32_t now_ms = (uint32_t)(esp_timer_get_time () / 1000);
            button_event_t event = button_sm_process (&sm, raw_pressed, now_ms);
            if (event != BUTTON_EVENT_NONE && s_context.callback != NULL)
                {
                    s_context.callback (event, s_context.user_data);
                }
            vTaskDelay (pdMS_TO_TICKS (BUTTON_POLL_INTERVAL_MS));
        }
}

esp_err_t
button_start (const button_config_t *config, button_event_cb_t callback, void *user_data)
{
    if (config == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    s_context.config = *config;
    s_context.callback = callback;
    s_context.user_data = user_data;

    gpio_config_t gpio = {
        .pin_bit_mask = 1ULL << config->gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR (gpio_config (&gpio), TAG, "configure BOOT GPIO");

    BaseType_t created
        = xTaskCreatePinnedToCore (button_task, "button", BUTTON_TASK_STACK_SIZE, NULL,
                                   BUTTON_TASK_PRIORITY, NULL, BUTTON_TASK_CORE);
    if (created != pdPASS)
        {
            return ESP_ERR_NO_MEM;
        }

    ESP_LOGI (TAG, "BOOT button on GPIO %d", config->gpio_num);
    return ESP_OK;
}
