/**
 * @file button_sm.h
 * @brief Pure button debounce and press-classification state machine.
 *
 * This module contains no hardware dependency: it consumes raw, sampled button
 * levels and timestamps and produces button_event_t values. It is the unit
 * under host test.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "button.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Debounce/classification state. */
    typedef struct
    {
        uint32_t debounce_ms;   /**< Stable time required to accept a change. */
        uint32_t long_press_ms; /**< Hold time that classifies a long press. */
        bool raw;               /**< Last raw sample (true = pressed). */
        bool debounced;         /**< Debounced button level (true = pressed). */
        uint32_t raw_change_ms; /**< Time of the last raw level change. */
        uint32_t press_ms;      /**< Time the debounced press started. */
        bool long_fired;        /**< Whether LONG_PRESS was already reported. */
    } button_sm_t;

    /**
     * @brief Initialize the state machine.
     *
     * @param sm             State machine; must not be NULL.
     * @param debounce_ms    Stable time required to accept a change.
     * @param long_press_ms  Hold time that classifies a long press.
     */
    void button_sm_init (button_sm_t *sm, uint32_t debounce_ms, uint32_t long_press_ms);

    /**
     * @brief Feed one raw sample into the state machine.
     *
     * @param sm          State machine; must not be NULL.
     * @param raw_pressed Raw button level (true = pressed).
     * @param now_ms      Monotonic time in milliseconds.
     * @return The event produced by this step, or BUTTON_EVENT_NONE.
     */
    button_event_t button_sm_process (button_sm_t *sm, bool raw_pressed, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
