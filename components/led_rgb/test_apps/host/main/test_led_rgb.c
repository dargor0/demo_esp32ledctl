#include "led_rgb.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

static led_rgb_output_t s_last;
static int s_render_count;

static void
capture_sink (const led_rgb_output_t *out)
{
    s_last = *out;
    s_render_count++;
}

static void
reset (void)
{
    led_rgb_init ();
    led_rgb_set_sink (capture_sink);
    memset (&s_last, 0, sizeof (s_last));
    s_render_count = 0;
}

TEST_CASE ("provisioning is a slow blue blink", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_PROVISIONING);

    led_rgb_output_t out;
    led_rgb_get_output (0, &out);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_PROVISIONING, (int)out.state);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.r);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.g);
    TEST_ASSERT_EQUAL_UINT8 (255, out.color.b);
    TEST_ASSERT_EQUAL_UINT32 (LED_RGB_SLOW_BLINK_MS, out.blink_ms);
    TEST_ASSERT_TRUE (out.on);

    led_rgb_get_output (LED_RGB_SLOW_BLINK_MS * 3 / 4, &out);
    TEST_ASSERT_FALSE (out.on);
}

TEST_CASE ("connecting is a fast amber blink", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTING);

    led_rgb_output_t out;
    led_rgb_get_output (0, &out);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTING, (int)out.state);
    TEST_ASSERT_EQUAL_UINT8 (255, out.color.r);
    TEST_ASSERT_EQUAL_UINT8 (191, out.color.g);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.b);
    TEST_ASSERT_EQUAL_UINT32 (LED_RGB_FAST_BLINK_MS, out.blink_ms);
    TEST_ASSERT_TRUE (out.on);

    led_rgb_get_output (LED_RGB_FAST_BLINK_MS * 3 / 4, &out);
    TEST_ASSERT_FALSE (out.on);
}

TEST_CASE ("connected is solid green", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_output_t out;
    led_rgb_get_output (1234567, &out);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTED, (int)out.state);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.r);
    TEST_ASSERT_EQUAL_UINT8 (255, out.color.g);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.b);
    TEST_ASSERT_EQUAL_UINT32 (0, out.blink_ms);
    TEST_ASSERT_TRUE (out.on);
}

TEST_CASE ("error is a fast red blink", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_ERROR);

    led_rgb_output_t out;
    led_rgb_get_output (0, &out);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_ERROR, (int)out.state);
    TEST_ASSERT_EQUAL_UINT8 (255, out.color.r);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.g);
    TEST_ASSERT_EQUAL_UINT8 (0, out.color.b);
    TEST_ASSERT_EQUAL_UINT32 (LED_RGB_FAST_BLINK_MS, out.blink_ms);

    led_rgb_get_output (LED_RGB_FAST_BLINK_MS * 3 / 4, &out);
    TEST_ASSERT_FALSE (out.on);
}

TEST_CASE ("custom requires the connected state", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_PROVISIONING);

    led_rgb_color_t color = { 10, 20, 30 };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, led_rgb_set_custom (color, 0));
    TEST_ASSERT_FALSE (led_rgb_is_custom ());
}

TEST_CASE ("custom color and blink are applied", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_color_t color = { 10, 20, 30 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (color, 1000));
    TEST_ASSERT_TRUE (led_rgb_is_custom ());
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CUSTOM, (int)led_rgb_get_state ());

    led_rgb_output_t out;
    led_rgb_get_output (0, &out);
    TEST_ASSERT_EQUAL_UINT8 (10, out.color.r);
    TEST_ASSERT_EQUAL_UINT8 (20, out.color.g);
    TEST_ASSERT_EQUAL_UINT8 (30, out.color.b);
    TEST_ASSERT_EQUAL_UINT32 (1000, out.blink_ms);
    TEST_ASSERT_TRUE (out.on);

    led_rgb_get_output (600, &out);
    TEST_ASSERT_FALSE (out.on);
}

TEST_CASE ("clearing custom returns to connected", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_color_t color = { 1, 2, 3 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (color, 0));
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_clear_custom ());
    TEST_ASSERT_FALSE (led_rgb_is_custom ());
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTED, (int)led_rgb_get_state ());
}

TEST_CASE ("clearing custom when not custom fails", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, led_rgb_clear_custom ());
}

TEST_CASE ("solid custom is always on", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_color_t color = { 5, 6, 7 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (color, 0));

    led_rgb_output_t out;
    led_rgb_get_output (999999, &out);
    TEST_ASSERT_EQUAL_UINT32 (0, out.blink_ms);
    TEST_ASSERT_TRUE (out.on);
}

TEST_CASE ("custom blink above the maximum is rejected", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_color_t color = { 5, 6, 7 };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           led_rgb_set_custom (color, LED_RGB_MAX_BLINK_MS + 1));
    TEST_ASSERT_FALSE (led_rgb_is_custom ());
}

TEST_CASE ("disconnecting clears the custom state", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_color_t color = { 5, 6, 7 };
    TEST_ASSERT_EQUAL_INT (ESP_OK, led_rgb_set_custom (color, 0));

    led_rgb_set_state (LED_RGB_STATE_CONNECTING);
    TEST_ASSERT_FALSE (led_rgb_is_custom ());
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTING, (int)led_rgb_get_state ());
}

TEST_CASE ("render forwards the resolved output to the sink", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTED);

    led_rgb_render (0);
    TEST_ASSERT_EQUAL_INT (1, s_render_count);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTED, (int)s_last.state);
}

TEST_CASE ("render without a sink is a no-op", "[led_rgb]")
{
    reset ();
    led_rgb_set_sink (NULL);
    led_rgb_render (0);
    TEST_ASSERT_EQUAL_INT (0, s_render_count);
}

TEST_CASE ("invalid state and null output are ignored", "[led_rgb]")
{
    reset ();
    led_rgb_set_state (LED_RGB_STATE_CONNECTING);

    led_rgb_set_state ((led_rgb_state_t)99);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTING, (int)led_rgb_get_state ());

    led_rgb_set_state (LED_RGB_STATE_CUSTOM);
    TEST_ASSERT_EQUAL_INT (LED_RGB_STATE_CONNECTING, (int)led_rgb_get_state ());

    led_rgb_get_output (0, NULL);
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
