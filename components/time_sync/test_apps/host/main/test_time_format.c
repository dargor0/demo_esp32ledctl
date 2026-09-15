#include "time_format.h"
#include "unity.h"

#include <stdlib.h>
#include <time.h>

#define OUT_SIZE 32

static char s_out[OUT_SIZE];

static void
set_timezone (const char *tz)
{
    setenv ("TZ", tz, 1);
    tzset ();
}

TEST_CASE ("the epoch formats as ISO-8601 UTC", "[time_sync]")
{
    set_timezone ("UTC0");
    TEST_ASSERT_EQUAL_INT (ESP_OK, time_format_iso8601 (0, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("1970-01-01T00:00:00Z", s_out);
}

TEST_CASE ("a known timestamp formats as ISO-8601 UTC", "[time_sync]")
{
    set_timezone ("UTC0");
    TEST_ASSERT_EQUAL_INT (ESP_OK, time_format_iso8601 (1609459200LL, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("2021-01-01T00:00:00Z", s_out);
    TEST_ASSERT_EQUAL_INT (ESP_OK, time_format_iso8601 (1700000000LL, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("2023-11-14T22:13:20Z", s_out);
}

TEST_CASE ("local formatting honors the timezone", "[time_sync]")
{
    set_timezone ("UTC0");
    TEST_ASSERT_EQUAL_INT (ESP_OK, time_format_iso8601_local (1609459200LL, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("2021-01-01T00:00:00+0000", s_out);

    set_timezone ("EST5");
    TEST_ASSERT_EQUAL_INT (ESP_OK, time_format_iso8601_local (1609459200LL, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("2020-12-31T19:00:00-0500", s_out);
}

TEST_CASE ("a short buffer reports an invalid size", "[time_sync]")
{
    set_timezone ("UTC0");
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, time_format_iso8601 (0, s_out, 5));
    TEST_ASSERT_EQUAL_STRING ("", s_out);
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, time_format_iso8601_local (0, s_out, 5));
    TEST_ASSERT_EQUAL_STRING ("", s_out);
}

TEST_CASE ("null and zero-size buffers are rejected", "[time_sync]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, time_format_iso8601 (0, NULL, OUT_SIZE));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, time_format_iso8601 (0, s_out, 0));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, time_format_iso8601_local (0, NULL, OUT_SIZE));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, time_format_iso8601_local (0, s_out, 0));
}

TEST_CASE ("epoch validity tracks the synchronization threshold", "[time_sync]")
{
    TEST_ASSERT_FALSE (time_sync_is_valid_epoch (0));
    TEST_ASSERT_FALSE (time_sync_is_valid_epoch (TIME_SYNC_VALID_EPOCH - 1));
    TEST_ASSERT_TRUE (time_sync_is_valid_epoch (TIME_SYNC_VALID_EPOCH));
    TEST_ASSERT_TRUE (time_sync_is_valid_epoch (TIME_SYNC_VALID_EPOCH + 1));
}

TEST_CASE ("an out-of-range epoch is rejected", "[time_sync]")
{
    set_timezone ("UTC0");
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           time_format_iso8601 (INT64_MAX, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           time_format_iso8601_local (INT64_MAX, s_out, sizeof (s_out)));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
