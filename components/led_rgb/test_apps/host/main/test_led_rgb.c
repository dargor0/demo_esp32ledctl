#include "led_rgb.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

static led_rgb_output_t s_frame[LED_RGB_COUNT];
static size_t s_frame_count;

static void
capture_sink (const led_rgb_output_t *frame, size_t count)
{
    size_t n = count < LED_RGB_COUNT ? count : LED_RGB_COUNT;
    memcpy (s_frame, frame, n * sizeof (frame[0]));
    s_frame_count = count;
}

static void
reset (void)
{
    led_rgb_init ();
    led_rgb_set_sink (capture_sink);
    memset (s_frame, 0, sizeof (s_frame));
    s_frame_count = 0;
}

static led_rgb_color_t
color_of (uint16_t id, uint32_t now)
{
    led_rgb_output_t out;
    led_rgb_get_output (id, now, &out);
    return out.color;
}

TEST_CASE ("the LED count is fixed at compile time", "[led_rgb]")
{
    reset ();
    TEST_ASSERT_EQUAL_UINT (LED_RGB_COUNT, led_rgb_get_count ());
}

TEST_CASE ("provisioning is a slow blue blink on all LEDs", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_PROVISIONING);

    for (uint16_t id = 0; id < LED_RGB_COUNT; id++)
        {
            led_rgb_output_t out;
            led_rgb_get_output (id, 0, &out);
            TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_PROVISIONING, (int)out.state);
            TEST_ASSERT_EQUAL_UINT8 (0, out.color.r);
            TEST_ASSERT_EQUAL_UINT8 (0, out.color.g);
            TEST_ASSERT_EQUAL_UINT8 (255, out.color.b);
            TEST_ASSERT_EQUAL_UINT32 (LED_RGB_SLOW_BLINK_MS, out.blink_ms);
            TEST_ASSERT_TRUE (out.on);

            led_rgb_get_output (id, LED_RGB_SLOW_BLINK_MS * 3 / 4, &out);
            TEST_ASSERT_FALSE (out.on);
        }
}

TEST_CASE ("connected is solid green on all LEDs", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    for (uint16_t id = 0; id < LED_RGB_COUNT; id++)
        {
            led_rgb_output_t out;
            led_rgb_get_output (id, 1234567, &out);
            TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTED, (int)out.state);
            TEST_ASSERT_EQUAL_UINT8 (0, out.color.r);
            TEST_ASSERT_EQUAL_UINT8 (255, out.color.g);
            TEST_ASSERT_FALSE (out.blink_ms);
            TEST_ASSERT_TRUE (out.on);
        }
}

TEST_CASE ("error is a fast red blink", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_ERROR);
    led_rgb_output_t out;
    led_rgb_get_output (0, 0, &out);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_ERROR, (int)out.state);
    TEST_ASSERT_EQUAL_UINT8 (255, out.color.r);
    TEST_ASSERT_EQUAL_UINT32 (LED_RGB_FAST_BLINK_MS, out.blink_ms);
    led_rgb_get_output (0, LED_RGB_FAST_BLINK_MS * 3 / 4, &out);
    TEST_ASSERT_FALSE (out.on);
}

TEST_CASE ("a single LED can be set to a custom color", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    const led_rgb_color_t color = { 10, 20, 30 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (2, color, 1000));
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CUSTOM, (int)led_rgb_get_state (2));
    TEST_ASSERT_TRUE (led_rgb_is_custom (2));
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTED, (int)led_rgb_get_state (1));

    led_rgb_output_t out;
    led_rgb_get_output (2, 0, &out);
    TEST_ASSERT_EQUAL_UINT8 (10, out.color.r);
    TEST_ASSERT_EQUAL_UINT8 (20, out.color.g);
    TEST_ASSERT_EQUAL_UINT8 (30, out.color.b);
    TEST_ASSERT_EQUAL_UINT32 (1000, out.blink_ms);
    TEST_ASSERT_TRUE (out.on);
    led_rgb_get_output (2, 600, &out);
    TEST_ASSERT_FALSE (out.on);
}

TEST_CASE ("invalid ids and blink are rejected", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    const led_rgb_color_t color = { 1, 2, 3 };

    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, led_rgb_set_custom (LED_RGB_COUNT, color, 0));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           led_rgb_set_custom (0, color, LED_RGB_MAX_BLINK_MS + 1));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, led_rgb_clear_custom (LED_RGB_COUNT));
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_ERROR, (int)led_rgb_get_state (LED_RGB_COUNT));
}

TEST_CASE ("setting custom while not connected fails and changes nothing", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_PROVISIONING);
    const led_rgb_color_t color = { 1, 2, 3 };

    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, led_rgb_set_custom (0, color, 0));
    TEST_ASSERT_FALSE (led_rgb_is_custom (0));

    const led_rgb_cmd_t cmd = { .id = 0, .color = color, .blink_ms = 0 };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, led_rgb_set_custom_many (&cmd, 1));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, led_rgb_set_custom_all (color, 0));
}

TEST_CASE ("several LEDs can be set at once", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    const led_rgb_cmd_t cmds[] = {
        { .id = 0, .color = { 1, 0, 0 }, .blink_ms = 0 },
        { .id = 2, .color = { 0, 2, 0 }, .blink_ms = 500 },
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_many (cmds, 2));

    TEST_ASSERT_EQUAL_UINT8 (1, color_of (0, 0).r);
    TEST_ASSERT_EQUAL_UINT8 (2, color_of (2, 0).g);
    TEST_ASSERT_FALSE (led_rgb_is_custom (1));
}

TEST_CASE ("a bad entry rejects the whole array and changes nothing", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    const led_rgb_cmd_t good = { .id = 0, .color = { 9, 9, 9 }, .blink_ms = 0 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_many (&good, 1));

    const led_rgb_cmd_t cmds[] = {
        { .id = 1, .color = { 1, 1, 1 }, .blink_ms = 0 },
        { .id = LED_RGB_COUNT, .color = { 1, 1, 1 }, .blink_ms = 0 },
    };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, led_rgb_set_custom_many (cmds, 2));

    TEST_ASSERT_FALSE (led_rgb_is_custom (1));
    TEST_ASSERT_EQUAL_UINT8 (9, color_of (0, 0).r);
}

TEST_CASE ("setting all LEDs applies one color", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    const led_rgb_color_t color = { 7, 8, 9 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_all (color, 0));
    for (uint16_t id = 0; id < LED_RGB_COUNT; id++)
        {
            TEST_ASSERT_TRUE (led_rgb_is_custom (id));
            TEST_ASSERT_EQUAL_UINT8 (7, color_of (id, 0).r);
        }
}

TEST_CASE ("clearing custom is idempotent", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    const led_rgb_color_t color = { 1, 2, 3 };

    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (1, color, 0));
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_clear_custom (1));
    TEST_ASSERT_FALSE (led_rgb_is_custom (1));
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_clear_custom (1));
}

TEST_CASE ("clearing several LEDs and all of them", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_all ((led_rgb_color_t){ 1, 1, 1 }, 0));

    const uint16_t ids[] = { 0, 1 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_clear_custom_many (ids, 2));
    TEST_ASSERT_FALSE (led_rgb_is_custom (0));
    TEST_ASSERT_FALSE (led_rgb_is_custom (1));
    TEST_ASSERT_TRUE (led_rgb_is_custom (2));

    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_clear_custom_all ());
    for (uint16_t id = 0; id < LED_RGB_COUNT; id++)
        {
            TEST_ASSERT_FALSE (led_rgb_is_custom (id));
        }
}

TEST_CASE ("clear with a bad id rejects the whole call", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_all ((led_rgb_color_t){ 1, 1, 1 }, 0));

    const uint16_t ids[] = { 0, LED_RGB_COUNT };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, led_rgb_clear_custom_many (ids, 2));
    TEST_ASSERT_TRUE (led_rgb_is_custom (0));
}

TEST_CASE ("disconnecting clears every custom override", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_all ((led_rgb_color_t){ 5, 5, 5 }, 0));

    led_rgb_set_state (LED_RGB_STATE_CONNECTING);
    for (uint16_t id = 0; id < LED_RGB_COUNT; id++)
        {
            TEST_ASSERT_FALSE (led_rgb_is_custom (id));
        }
}

TEST_CASE ("render forwards the whole frame to the sink", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (0, (led_rgb_color_t){ 4, 5, 6 }, 0));

    led_rgb_render (0);
    TEST_ASSERT_EQUAL_UINT (LED_RGB_COUNT, s_frame_count);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CUSTOM, (int)s_frame[0].state);
    TEST_ASSERT_EQUAL_UINT8 (4, s_frame[0].color.r);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTED, (int)s_frame[1].state);
}

TEST_CASE ("output for an invalid id is an error", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    led_rgb_output_t out;
    led_rgb_get_output (LED_RGB_COUNT, 0, &out);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_ERROR, (int)out.state);
    TEST_ASSERT_FALSE (out.on);
    led_rgb_get_output (0, 0, NULL);
}

TEST_CASE ("null lists and a detached sink are handled", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, led_rgb_set_custom_many (NULL, 1));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, led_rgb_clear_custom_many (NULL, 1));
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom_many (NULL, 0));

    led_rgb_set_sink (NULL);
    led_rgb_render (0);
    TEST_ASSERT_EQUAL_UINT (0, s_frame_count);
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
