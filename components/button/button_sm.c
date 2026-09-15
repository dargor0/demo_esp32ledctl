#include "button_sm.h"

#include <stddef.h>

void
button_sm_init (button_sm_t *sm, uint32_t debounce_ms, uint32_t long_press_ms)
{
    if (sm == NULL)
        {
            return;
        }
    sm->debounce_ms = debounce_ms;
    sm->long_press_ms = long_press_ms;
    sm->raw = false;
    sm->debounced = false;
    sm->raw_change_ms = 0;
    sm->press_ms = 0;
    sm->long_fired = false;
}

/**
 * @brief Track the raw level and the time of its last change.
 *
 * @param sm   State machine.
 * @param raw  Raw button level.
 * @param now  Current time in milliseconds.
 */
static void
button_sm_track_raw (button_sm_t *sm, bool raw, uint32_t now)
{
    if (raw != sm->raw)
        {
            sm->raw = raw;
            sm->raw_change_ms = now;
        }
}

/**
 * @brief Accept a raw level that has been stable for the debounce time.
 *
 * @param sm   State machine.
 * @param now  Current time in milliseconds.
 * @return The debounce edge event, or BUTTON_EVENT_NONE.
 */
static button_event_t
button_sm_apply_debounce (button_sm_t *sm, uint32_t now)
{
    if (sm->raw == sm->debounced || (now - sm->raw_change_ms) < sm->debounce_ms)
        {
            return BUTTON_EVENT_NONE;
        }

    sm->debounced = sm->raw;
    if (sm->debounced)
        {
            sm->press_ms = now;
            sm->long_fired = false;
            return BUTTON_EVENT_PRESSED;
        }
    return sm->long_fired ? BUTTON_EVENT_RELEASED : BUTTON_EVENT_SHORT_PRESS;
}

/**
 * @brief Check whether the held button has reached the long-press threshold.
 *
 * @param sm   State machine.
 * @param now  Current time in milliseconds.
 * @return true if a long press should be reported now.
 */
static bool
button_sm_long_due (const button_sm_t *sm, uint32_t now)
{
    if (!sm->debounced || sm->long_fired || sm->long_press_ms == 0)
        {
            return false;
        }
    return (now - sm->press_ms) >= sm->long_press_ms;
}

button_event_t
button_sm_process (button_sm_t *sm, bool raw_pressed, uint32_t now_ms)
{
    if (sm == NULL)
        {
            return BUTTON_EVENT_NONE;
        }

    button_sm_track_raw (sm, raw_pressed, now_ms);

    button_event_t event = button_sm_apply_debounce (sm, now_ms);
    if (event != BUTTON_EVENT_NONE)
        {
            return event;
        }

    if (button_sm_long_due (sm, now_ms))
        {
            sm->long_fired = true;
            return BUTTON_EVENT_LONG_PRESS;
        }

    return BUTTON_EVENT_NONE;
}
