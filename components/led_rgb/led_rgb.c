/**
 * @file led_rgb.c
 * @brief LED state machine for one or more chained WS2812 LEDs.
 *
 * ALGORITHM OVERVIEW
 * ------------------
 * Two orthogonal pieces of state are kept:
 *   1. A single *automatic* state (provisioning / connecting / connected /
 *      error) that applies to every LED not individually overridden.
 *   2. A per-LED *custom* flag + color + blink period, set from the web UI.
 *
 * Resolving a frame is therefore:
 *   for each LED id:
 *       if custom_active[id] -> use the custom color/blink (state = CUSTOM)
 *       else                 -> use the pattern of the automatic state
 *       on = blink_on(blink_ms, now_ms)
 *
 * CONCURRENCY
 * -----------
 * The automatic state and the per-LED custom arrays are written from several
 * tasks (led_task renders on core 1; net/prov/button tasks and the HTTP server
 * task on core 0 call the setters). Every public function takes a single
 * recursive mutex for the whole read-modify-write, so e.g. "set custom" and
 * "clear custom" cannot interleave and leave a torn frame. On the host test
 * build the mutex is compiled out (no FreeRTOS available).
 *
 * render() is the one exception to "hold the lock for the whole call": it holds
 * the lock only long enough to snapshot the resolved frame into a static array,
 * then releases it before the blocking RMT transmit (FR-9).
 */
#include "led_rgb.h"

#include <stddef.h>

/*
 * Lock shim. On ESP targets this is a recursive mutex created on first use. On
 * the host (Linux target) it is a no-op so the pure logic runs without FreeRTOS.
 */
#ifndef CONFIG_IDF_TARGET_LINUX
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_lock;
static void
led_rgb_lock_init (void)
{
    if (s_lock == NULL)
        {
            s_lock = xSemaphoreCreateRecursiveMutex ();
        }
}
static void
led_rgb_lock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreTakeRecursive (s_lock, portMAX_DELAY);
        }
}
static void
led_rgb_unlock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreGiveRecursive (s_lock);
        }
}
#else
static void
led_rgb_lock_init (void)
{
}
static void
led_rgb_lock (void)
{
}
static void
led_rgb_unlock (void)
{
}
#endif

/** @brief Color and blink period associated with an automatic state. */
typedef struct
{
    led_rgb_color_t color;
    uint32_t blink_ms;
} led_rgb_pattern_t;

/**
 * Automatic patterns indexed by led_rgb_state_t.
 *
 * The blink periods come from Kconfig (LED_RGB_SLOW_BLINK_MS /
 * LED_RGB_FAST_BLINK_MS). A blink_ms of 0 means "always on" (solid).
 */
static const led_rgb_pattern_t s_patterns[LED_RGB_STATE_COUNT] = {
    [LED_RGB_STATE_PROVISIONING] = { { 0, 0, 255 }, LED_RGB_SLOW_BLINK_MS },
    [LED_RGB_STATE_CONNECTING] = { { 255, 191, 0 }, LED_RGB_FAST_BLINK_MS },
    [LED_RGB_STATE_CONNECTED] = { { 0, 255, 0 }, 0 },
    [LED_RGB_STATE_ERROR] = { { 255, 0, 0 }, LED_RGB_FAST_BLINK_MS },
    [LED_RGB_STATE_CUSTOM] = { { 0, 0, 0 }, 0 },
};

/* Automatic state shared by all LEDs. Guarded by s_lock. */
static led_rgb_state_t s_state = LED_RGB_STATE_PROVISIONING;
/* Per-LED custom overrides. Guarded by s_lock. */
static bool s_custom_active[LED_RGB_COUNT];
static led_rgb_color_t s_custom_color[LED_RGB_COUNT];
static uint32_t s_custom_blink_ms[LED_RGB_COUNT];
/* Output sink and the frame snapshot handed to it. */
static led_rgb_sink_t s_sink;
static led_rgb_output_t s_frame[LED_RGB_COUNT];

/** @brief Validate an LED ID. */
static bool
led_rgb_id_valid (uint16_t id)
{
    return id < LED_RGB_COUNT;
}

/**
 * @brief Range validation for a custom command.
 *
 * Color channels are uint8_t so they are always in range; only the id and the
 * blink period can be invalid.
 */
static bool
led_rgb_command_valid (const led_rgb_cmd_t *cmd)
{
    if (cmd == NULL || !led_rgb_id_valid (cmd->id))
        {
            return false;
        }
    return cmd->blink_ms <= LED_RGB_MAX_BLINK_MS;
}

/** @brief Commit a validated command into the per-LED arrays (lock held). */
static void
led_rgb_apply_command (const led_rgb_cmd_t *cmd)
{
    s_custom_active[cmd->id] = true;
    s_custom_color[cmd->id] = cmd->color;
    s_custom_blink_ms[cmd->id] = cmd->blink_ms;
}

/**
 * @brief Evaluate a blink pattern at a given time.
 *
 * The pattern spends the first half of each period "on" and the second half
 * "off": on = (now % period) < period/2. A period of 0 is solid on.
 */
static bool
led_rgb_blink_on (uint32_t blink_ms, uint32_t now_ms)
{
    if (blink_ms == 0)
        {
            return true;
        }
    return (now_ms % blink_ms) < (blink_ms / 2);
}

/**
 * @brief Resolve one LED's output (caller holds the lock).
 *
 * Sequence: start from the automatic pattern, then let a custom override
 * replace the state, color and blink period, then apply the blink evaluation.
 */
static void
led_rgb_compute_locked (uint16_t id, uint32_t now_ms, led_rgb_output_t *out)
{
    const led_rgb_pattern_t *pattern = &s_patterns[s_state];
    led_rgb_state_t state = s_state;
    led_rgb_color_t color = pattern->color;
    uint32_t blink_ms = pattern->blink_ms;

    if (s_custom_active[id])
        {
            state = LED_RGB_STATE_CUSTOM;
            color = s_custom_color[id];
            blink_ms = s_custom_blink_ms[id];
        }

    out->state = state;
    out->color = color;
    out->blink_ms = blink_ms;
    out->on = led_rgb_blink_on (blink_ms, now_ms);
}

/**
 * @brief Reset all state to power-on defaults.
 *
 * Creates the lock on first call, then clears the automatic state and every
 * per-LED custom override.
 */
void
led_rgb_init (void)
{
    led_rgb_lock_init ();
    led_rgb_lock ();
    s_state = LED_RGB_STATE_PROVISIONING;
    for (uint16_t i = 0; i < LED_RGB_COUNT; i++)
        {
            s_custom_active[i] = false;
            s_custom_color[i] = (led_rgb_color_t){ 0, 0, 0 };
            s_custom_blink_ms[i] = 0;
        }
    led_rgb_unlock ();
}

/** @brief Register the frame sink (init-time, still lock-protected). */
void
led_rgb_set_sink (led_rgb_sink_t sink)
{
    led_rgb_lock ();
    s_sink = sink;
    led_rgb_unlock ();
}

/**
 * @brief Set the automatic state for all LEDs.
 *
 * Sequence:
 *   1. Reject out-of-range states and the pseudo-state CUSTOM (custom is only
 *      entered via the set_custom* functions).
 *   2. Store the new automatic state.
 *   3. Clear every custom override, because the automatic state is authoritative
 *      and a state change (e.g. a WiFi disconnect) resets user overrides.
 */
void
led_rgb_set_state (led_rgb_state_t state)
{
    if (state >= LED_RGB_STATE_COUNT || state == LED_RGB_STATE_CUSTOM)
        {
            return;
        }
    led_rgb_lock ();
    s_state = state;
    for (uint16_t i = 0; i < LED_RGB_COUNT; i++)
        {
            s_custom_active[i] = false;
        }
    led_rgb_unlock ();
}

/** @brief Return CUSTOM for an overridden LED, otherwise the automatic state. */
led_rgb_state_t
led_rgb_get_state (uint16_t id)
{
    if (!led_rgb_id_valid (id))
        {
            return LED_RGB_STATE_ERROR;
        }
    led_rgb_lock ();
    led_rgb_state_t state = s_custom_active[id] ? LED_RGB_STATE_CUSTOM : s_state;
    led_rgb_unlock ();
    return state;
}

/** @brief Number of LEDs in the chain (compile-time constant). */
size_t
led_rgb_get_count (void)
{
    return LED_RGB_COUNT;
}

/**
 * @brief Set one LED to a custom color/blink.
 *
 * Validation order (FR-10): arguments first (id / blink), then the connection
 * gate, then the update. A failure at either step changes nothing.
 */
esp_err_t
led_rgb_set_custom (uint16_t id, led_rgb_color_t color, uint32_t blink_ms)
{
    if (!led_rgb_id_valid (id) || blink_ms > LED_RGB_MAX_BLINK_MS)
        {
            return ESP_ERR_INVALID_ARG;
        }

    led_rgb_lock ();
    if (s_state != LED_RGB_STATE_CONNECTED)
        {
            led_rgb_unlock ();
            return ESP_ERR_INVALID_STATE;
        }
    const led_rgb_cmd_t cmd = { .id = id, .color = color, .blink_ms = blink_ms };
    led_rgb_apply_command (&cmd);
    led_rgb_unlock ();
    return ESP_OK;
}

/**
 * @brief Apply an array of per-LED commands atomically (all-or-nothing).
 *
 * Sequence:
 *   1. Validate every entry (range checks) before touching any state.
 *   2. Take the lock and evaluate the connection gate once for the whole batch.
 *   3. Apply all commands, then release the lock.
 * If any entry is invalid the whole batch is rejected and no LED changes.
 */
esp_err_t
led_rgb_set_custom_many (const led_rgb_cmd_t *cmds, size_t count)
{
    if (cmds == NULL && count != 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    for (size_t i = 0; i < count; i++)
        {
            if (!led_rgb_command_valid (&cmds[i]))
                {
                    return ESP_ERR_INVALID_ARG;
                }
        }

    led_rgb_lock ();
    if (s_state != LED_RGB_STATE_CONNECTED)
        {
            led_rgb_unlock ();
            return ESP_ERR_INVALID_STATE;
        }
    for (size_t i = 0; i < count; i++)
        {
            led_rgb_apply_command (&cmds[i]);
        }
    led_rgb_unlock ();
    return ESP_OK;
}

/**
 * @brief Broadcast one custom color/blink to every LED atomically.
 *
 * Equivalent to set_custom_many() with all IDs, but without building an array.
 */
esp_err_t
led_rgb_set_custom_all (led_rgb_color_t color, uint32_t blink_ms)
{
    if (blink_ms > LED_RGB_MAX_BLINK_MS)
        {
            return ESP_ERR_INVALID_ARG;
        }

    led_rgb_lock ();
    if (s_state != LED_RGB_STATE_CONNECTED)
        {
            led_rgb_unlock ();
            return ESP_ERR_INVALID_STATE;
        }
    for (uint16_t i = 0; i < LED_RGB_COUNT; i++)
        {
            s_custom_active[i] = true;
            s_custom_color[i] = color;
            s_custom_blink_ms[i] = blink_ms;
        }
    led_rgb_unlock ();
    return ESP_OK;
}

/**
 * @brief Clear one LED's custom override.
 *
 * Idempotent: clearing an LED that is not custom is a success (no error).
 */
esp_err_t
led_rgb_clear_custom (uint16_t id)
{
    if (!led_rgb_id_valid (id))
        {
            return ESP_ERR_INVALID_ARG;
        }
    led_rgb_lock ();
    s_custom_active[id] = false;
    led_rgb_unlock ();
    return ESP_OK;
}

/**
 * @brief Clear several LEDs' custom overrides atomically.
 *
 * All ids are validated first; if any is invalid the whole call fails and no
 * LED is changed.
 */
esp_err_t
led_rgb_clear_custom_many (const uint16_t *ids, size_t count)
{
    if (ids == NULL && count != 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    for (size_t i = 0; i < count; i++)
        {
            if (!led_rgb_id_valid (ids[i]))
                {
                    return ESP_ERR_INVALID_ARG;
                }
        }
    led_rgb_lock ();
    for (size_t i = 0; i < count; i++)
        {
            s_custom_active[ids[i]] = false;
        }
    led_rgb_unlock ();
    return ESP_OK;
}

/** @brief Clear every LED's custom override (idempotent). */
esp_err_t
led_rgb_clear_custom_all (void)
{
    led_rgb_lock ();
    for (uint16_t i = 0; i < LED_RGB_COUNT; i++)
        {
            s_custom_active[i] = false;
        }
    led_rgb_unlock ();
    return ESP_OK;
}

/** @brief Check whether an LED currently has a custom override. */
bool
led_rgb_is_custom (uint16_t id)
{
    if (!led_rgb_id_valid (id))
        {
            return false;
        }
    led_rgb_lock ();
    bool custom = s_custom_active[id];
    led_rgb_unlock ();
    return custom;
}

/**
 * @brief Resolve one LED into a caller-owned structure.
 *
 * An invalid id yields an explicit ERROR output (off), which the REST layer
 * turns into a 404 before calling here; this is a defensive fallback.
 */
void
led_rgb_get_output (uint16_t id, uint32_t now_ms, led_rgb_output_t *out)
{
    if (out == NULL)
        {
            return;
        }

    led_rgb_lock ();
    if (!led_rgb_id_valid (id))
        {
            out->state = LED_RGB_STATE_ERROR;
            out->color = (led_rgb_color_t){ 0, 0, 0 };
            out->blink_ms = 0;
            out->on = false;
        }
    else
        {
            led_rgb_compute_locked (id, now_ms, out);
        }
    led_rgb_unlock ();
}

/**
 * @brief Resolve the whole chain and hand it to the sink.
 *
 * Sequence (FR-9 compliant):
 *   1. Take the lock and copy the sink pointer.
 *   2. Resolve every LED into the static frame snapshot.
 *   3. Release the lock BEFORE transmitting, so the blocking RMT transfer does
 *      not hold the mutex and starve the setters.
 *   4. Invoke the sink with the snapshot (the RMT driver then transmits).
 */
void
led_rgb_render (uint32_t now_ms)
{
    led_rgb_lock ();
    led_rgb_sink_t sink = s_sink;
    for (uint16_t i = 0; i < LED_RGB_COUNT; i++)
        {
            led_rgb_compute_locked (i, now_ms, &s_frame[i]);
        }
    led_rgb_unlock ();

    if (sink != NULL)
        {
            sink (s_frame, LED_RGB_COUNT);
        }
}
