/**
 * @file button.h
 * @brief BOOT button event interface.
 *
 * The BOOT button (active low) is debounced in software and classified into
 * short and long presses:
 *   - short press : released before the long-press threshold
 *   - long press  : held for at least the long-press threshold
 *
 * A long press is reported as soon as the threshold is reached, while the
 * button is still held. A short press is reported on release.
 *
 * The debounce and press-classification logic lives in the pure
 * button_sm_process() state machine (see button_sm.h) so it can be unit-tested
 * on the host without hardware. button_start() provides the ESP GPIO + task
 * integration.
 */
#pragma once

#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Default software debounce time in milliseconds. */
#define BUTTON_DEFAULT_DEBOUNCE_MS 50
/** @brief Default long-press threshold in milliseconds. */
#define BUTTON_DEFAULT_LONG_PRESS_MS 5000
/** @brief Task core used by the button task unless overridden by Kconfig. */
#ifndef CONFIG_APP_TASK_CORE_BUTTON
#define BUTTON_TASK_CORE 1
#else
#define BUTTON_TASK_CORE CONFIG_APP_TASK_CORE_BUTTON
#endif

    /** @brief Button events emitted by the state machine. */
    typedef enum
    {
        BUTTON_EVENT_NONE = 0,    /**< No event for this processing step. */
        BUTTON_EVENT_PRESSED,     /**< Debounced press edge. */
        BUTTON_EVENT_SHORT_PRESS, /**< Released before the long-press threshold. */
        BUTTON_EVENT_LONG_PRESS,  /**< Held for at least the long-press threshold. */
        BUTTON_EVENT_RELEASED,    /**< Released after a long press was reported. */
    } button_event_t;

    /** @brief Button configuration. */
    typedef struct
    {
        int gpio_num;           /**< GPIO connected to the button (active low). */
        uint32_t debounce_ms;   /**< Stable time required to accept a change. */
        uint32_t long_press_ms; /**< Hold time that classifies a long press. */
    } button_config_t;

    /**
     * @brief Callback invoked from the button task for each event.
     *
     * @param event      The event that occurred.
     * @param user_data  Opaque pointer passed to button_start().
     */
    typedef void (*button_event_cb_t) (button_event_t event, void *user_data);

    /**
     * @brief Get the human-readable name of an event.
     *
     * @param event  Button event.
     * @return Static string ("none", "pressed", "short_press", "long_press",
     *         "released").
     */
    const char *button_event_name (button_event_t event);

    /**
     * @brief Get the most recent button event (thread-safe).
     *
     * @return The last event, or BUTTON_EVENT_NONE if none has occurred.
     */
    button_event_t button_get_last_event (void);

    /**
     * @brief Configure the BOOT GPIO and start the button polling task.
     *
     * The task runs on BUTTON_TASK_CORE and must not block the caller.
     *
     * @param config     Button configuration; must not be NULL.
     * @param callback   Called for each event; may be NULL.
     * @param user_data  Passed back to the callback.
     * @return ESP_OK on success, otherwise an error from the GPIO or task layer.
     */
    esp_err_t button_start (const button_config_t *config, button_event_cb_t callback,
                            void *user_data);

#ifdef __cplusplus
}
#endif
