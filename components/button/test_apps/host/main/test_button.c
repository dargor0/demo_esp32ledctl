#include "button.h"
#include "button_sm.h"
#include "unity.h"

#include <stdlib.h>

/** @brief Debounce used by most tests, in milliseconds. */
#define TEST_DEBOUNCE_MS 50
/** @brief Long-press threshold used by most tests, in milliseconds. */
#define TEST_LONG_PRESS_MS 5000

static button_sm_t s_sm;

static void
reset (void)
{
    button_sm_init (&s_sm, TEST_DEBOUNCE_MS, TEST_LONG_PRESS_MS);
}

TEST_CASE ("an idle button produces no events", "[button]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 0));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 1000));
}

TEST_CASE ("a press is reported only after the debounce time", "[button]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 0));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 30));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_PRESSED, button_sm_process (&s_sm, true, 50));
}

TEST_CASE ("bouncing does not produce a press", "[button]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 0));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 10));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 20));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 30));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 90));
}

TEST_CASE ("a short press is reported on release", "[button]")
{
    reset ();
    button_sm_process (&s_sm, true, 0);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_PRESSED, button_sm_process (&s_sm, true, 50));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 100));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_SHORT_PRESS, button_sm_process (&s_sm, false, 150));
}

TEST_CASE ("a long press is reported at the threshold", "[button]")
{
    reset ();
    button_sm_process (&s_sm, true, 0);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_PRESSED, button_sm_process (&s_sm, true, 50));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_LONG_PRESS, button_sm_process (&s_sm, true, 5050));
}

TEST_CASE ("releasing after a long press does not report a short press", "[button]")
{
    reset ();
    button_sm_process (&s_sm, true, 0);
    button_sm_process (&s_sm, true, 50);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_LONG_PRESS, button_sm_process (&s_sm, true, 5050));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 6000));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_RELEASED, button_sm_process (&s_sm, false, 6050));
}

TEST_CASE ("a long press is reported only once while held", "[button]")
{
    reset ();
    button_sm_process (&s_sm, true, 0);
    button_sm_process (&s_sm, true, 50);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_LONG_PRESS, button_sm_process (&s_sm, true, 5050));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 7000));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 9000));
}

TEST_CASE ("a zero debounce reacts immediately", "[button]")
{
    button_sm_init (&s_sm, 0, TEST_LONG_PRESS_MS);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_PRESSED, button_sm_process (&s_sm, true, 0));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_SHORT_PRESS, button_sm_process (&s_sm, false, 10));
}

TEST_CASE ("two consecutive presses produce two short presses", "[button]")
{
    reset ();
    button_sm_process (&s_sm, true, 0);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_PRESSED, button_sm_process (&s_sm, true, 50));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 150));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_SHORT_PRESS, button_sm_process (&s_sm, false, 200));

    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, true, 250));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_PRESSED, button_sm_process (&s_sm, true, 300));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (&s_sm, false, 350));
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_SHORT_PRESS, button_sm_process (&s_sm, false, 400));
}

TEST_CASE ("null pointers are handled safely", "[button]")
{
    button_sm_init (NULL, 50, 5000);
    TEST_ASSERT_EQUAL_INT (BUTTON_EVENT_NONE, button_sm_process (NULL, true, 0));
}

TEST_CASE ("event names are human readable", "[button]")
{
    TEST_ASSERT_EQUAL_STRING ("none", button_event_name (BUTTON_EVENT_NONE));
    TEST_ASSERT_EQUAL_STRING ("pressed", button_event_name (BUTTON_EVENT_PRESSED));
    TEST_ASSERT_EQUAL_STRING ("short_press", button_event_name (BUTTON_EVENT_SHORT_PRESS));
    TEST_ASSERT_EQUAL_STRING ("long_press", button_event_name (BUTTON_EVENT_LONG_PRESS));
    TEST_ASSERT_EQUAL_STRING ("released", button_event_name (BUTTON_EVENT_RELEASED));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
