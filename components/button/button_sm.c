/**
 * @file button_sm.c
 * @brief Pure debounce + press-classification state machine.
 *
 * ALGORITHM
 * ---------
 * button_sm_process() is called periodically (every few ms) with the current
 * raw GPIO level and a monotonic timestamp. It is a "sample in, at most one
 * event out" machine so it drives no hardware and is trivially testable.
 *
 * Debounce:
 *   The machine remembers the last raw level and when it changed. A raw level
 *   is only *accepted* once it has been stable for `debounce_ms`; until then the
 *   previous debounced level is kept, which filters contact bounce.
 *
 * Press classification (sequence):
 *   1. Track the raw level / change time.
 *   2. If a stable edge is accepted:
 *        - press edge  -> PRESSED, start the press timer, clear long_fired;
 *        - release edge -> RELEASED (if a long press was already reported) or
 *          SHORT_PRESS (released before the long-press threshold).
 *   3. Otherwise, if still held and the hold time reached `long_press_ms`,
 *      report LONG_PRESS once (long_fired latches it).
 *
 * A long press fires while the button is still held; a short press fires on
 * release. Only one event is returned per call.
 */
#include "button_sm.h"

#include <stddef.h>

/** @brief Reset the state machine to "not pressed". */
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
 * The timestamp of the most recent raw transition is what the debounce logic
 * measures stability against.
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
 * Sequence:
 *   1. Do nothing unless the raw level differs from the debounced level AND it
 *      has been stable for `debounce_ms`.
 *   2. On a press edge: start the press timer, clear the long-press latch and
 *      return PRESSED.
 *   3. On a release edge: return RELEASED if a long press was already reported,
 *      otherwise SHORT_PRESS.
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

/**
 * @brief Feed one raw sample and return at most one event.
 *
 * Order matters: a debounce edge is returned before the long-press check so
 * that the press/release bookkeeping is never skipped. Long press is evaluated
 * only when no edge occurred on this sample.
 */
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

/** @brief Map an event to a stable API string (used by /api/status). */
const char *
button_event_name (button_event_t event)
{
    switch (event)
        {
        case BUTTON_EVENT_PRESSED:
            return "pressed";
        case BUTTON_EVENT_SHORT_PRESS:
            return "short_press";
        case BUTTON_EVENT_LONG_PRESS:
            return "long_press";
        case BUTTON_EVENT_RELEASED:
            return "released";
        case BUTTON_EVENT_NONE:
        default:
            return "none";
        }
}
