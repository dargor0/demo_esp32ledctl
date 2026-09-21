/**
 * @file button_gpio.c
 * @brief ESP integration for the BOOT button: GPIO polling + task + events.
 *
 * SEQUENCE
 * --------
 * button_start() configures the GPIO as an input with a pull-up (the BOOT
 * button is active-low), creates a mutex for the "last event" field and spawns
 * the button task pinned to BUTTON_TASK_CORE.
 *
 * The task loop, every BUTTON_POLL_INTERVAL_MS:
 *   1. samples the GPIO (0 = pressed) and reads the monotonic clock;
 *   2. feeds the sample to the pure state machine (button_sm_process);
 *   3. if an event was produced, records it (thread-safe) and invokes the
 *      user callback.
 *
 * Thread safety: the state machine is owned exclusively by this task, so only
 * the cross-task "last event" field needs a lock (read by the HTTP server).
 */
#include "button.h"

#include <string.h>

#include "button_sm.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/** @brief GPIO polling period in milliseconds. */
#define BUTTON_POLL_INTERVAL_MS 10
/** @brief Priority for the button task (fixed in code). */
#define BUTTON_TASK_PRIORITY 5

/* Stack size from Kconfig, falling back to a sane default for host/standalone. */
#ifdef CONFIG_APP_TASK_STACK_BUTTON
#define BUTTON_TASK_STACK_SIZE CONFIG_APP_TASK_STACK_BUTTON
#else
#define BUTTON_TASK_STACK_SIZE 3072
#endif

static const char *TAG = "button";

/** @brief Module context shared with the button task. */
typedef struct
{
    button_config_t config;
    button_event_cb_t callback;
    void *user_data;
} button_context_t;

static button_context_t s_context;
/* Last event, exposed to /api/status; guarded by s_event_lock. */
static button_event_t s_last_event = BUTTON_EVENT_NONE;
static SemaphoreHandle_t s_event_lock;

/** @brief Read the last event under the lock (safe from any task). */
button_event_t
button_get_last_event (void)
{
    if (s_event_lock == NULL)
        {
            return BUTTON_EVENT_NONE;
        }
    xSemaphoreTake (s_event_lock, portMAX_DELAY);
    button_event_t event = s_last_event;
    xSemaphoreGive (s_event_lock);
    return event;
}

/** @brief Record the latest event (thread-safe). */
static void
button_store_event (button_event_t event)
{
    if (s_event_lock == NULL)
        {
            return;
        }
    xSemaphoreTake (s_event_lock, portMAX_DELAY);
    s_last_event = event;
    xSemaphoreGive (s_event_lock);
}

/**
 * @brief Poll the GPIO, run the state machine and dispatch events.
 *
 * @param arg  Unused.
 */
static void
button_task (void *arg)
{
    (void)arg;
    /* The state machine lives on this task's stack (single owner). */
    button_sm_t sm;
    button_sm_init (&sm, s_context.config.debounce_ms, s_context.config.long_press_ms);

    for (;;)
        {
            /* Active-low: level 0 means pressed. */
            bool raw_pressed = gpio_get_level (s_context.config.gpio_num) == 0;
            uint32_t now_ms = (uint32_t)(esp_timer_get_time () / 1000);
            button_event_t event = button_sm_process (&sm, raw_pressed, now_ms);
            if (event != BUTTON_EVENT_NONE)
                {
                    button_store_event (event);
                    if (s_context.callback != NULL)
                        {
                            s_context.callback (event, s_context.user_data);
                        }
                }
            vTaskDelay (pdMS_TO_TICKS (BUTTON_POLL_INTERVAL_MS));
        }
}

/**
 * @brief Configure the BOOT GPIO and start the polling task.
 *
 * Sequence: validate args, create the event mutex (once), store the context,
 * configure the GPIO, then create the pinned task. Idempotent-ish: calling
 * twice would start two tasks, so it is intended to be called once from main.
 */
esp_err_t
button_start (const button_config_t *config, button_event_cb_t callback, void *user_data)
{
    if (config == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    if (s_event_lock == NULL)
        {
            s_event_lock = xSemaphoreCreateMutex ();
            if (s_event_lock == NULL)
                {
                    return ESP_ERR_NO_MEM;
                }
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
