#include "api_json.h"
#include "unity.h"

#include <stdlib.h>

static char s_buf[256];

TEST_CASE ("a full LED command is parsed", "[http_server]")
{
    const char *body = "{\"r\":10,\"g\":20,\"b\":30,\"blink_ms\":500}";
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_parse_led (body, &cmd));
    TEST_ASSERT_EQUAL_INT (10, cmd.r);
    TEST_ASSERT_EQUAL_INT (20, cmd.g);
    TEST_ASSERT_EQUAL_INT (30, cmd.b);
    TEST_ASSERT_EQUAL_UINT32 (500, cmd.blink_ms);
}

TEST_CASE ("blink_ms is optional and defaults to solid", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_parse_led ("{\"r\":1,\"g\":2,\"b\":3}", &cmd));
    TEST_ASSERT_EQUAL_UINT32 (0, cmd.blink_ms);
}

TEST_CASE ("missing channels are rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led ("{\"r\":1,\"g\":2}", &cmd));
}

TEST_CASE ("out-of-range channels are rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"r\":256,\"g\":0,\"b\":0}", &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"r\":0,\"g\":-1,\"b\":0}", &cmd));
}

TEST_CASE ("an out-of-range blink period is rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (
        ESP_ERR_INVALID_ARG,
        api_json_parse_led ("{\"r\":0,\"g\":0,\"b\":0,\"blink_ms\":60001}", &cmd));
}

TEST_CASE ("a negative blink period is rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"r\":0,\"g\":0,\"b\":0,\"blink_ms\":-5}", &cmd));
}

TEST_CASE ("non-numeric channels are rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"r\":\"x\",\"g\":0,\"b\":0}", &cmd));
}

TEST_CASE ("malformed and null bodies are rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led ("not json", &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led (NULL, &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"r\":0,\"g\":0,\"b\":0}", NULL));
}

TEST_CASE ("the status response is formatted as JSON", "[http_server]")
{
    const api_status_t status = {
        .system = "esp32ledctl",
        .state = "connected",
        .wifi = "connected",
        .ip = "192.168.1.5",
        .uptime_s = 123,
        .free_heap = 45678,
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_status (&status, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"system\":\"esp32ledctl\",\"state\":\"connected\","
                              "\"wifi\":\"connected\",\"ip\":\"192.168.1.5\","
                              "\"uptime_s\":123,\"free_heap\":45678}",
                              s_buf);
}

TEST_CASE ("the LED response is formatted as JSON", "[http_server]")
{
    const api_led_state_t led = {
        .state = "custom",
        .r = 10,
        .g = 20,
        .b = 30,
        .blink_ms = 0,
        .on = true,
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_led (&led, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"state\":\"custom\",\"r\":10,\"g\":20,\"b\":30,"
                              "\"blink_ms\":0,\"on\":true}",
                              s_buf);
}

TEST_CASE ("the time response reflects the sync state", "[http_server]")
{
    const api_time_t synced = {
        .synced = true,
        .epoch = 1609459200,
        .iso8601 = "2021-01-01T00:00:00Z",
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_time (&synced, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"synced\":true,\"epoch\":1609459200,"
                              "\"iso8601\":\"2021-01-01T00:00:00Z\"}",
                              s_buf);

    const api_time_t unsynced = { .synced = false, .epoch = 0, .iso8601 = "" };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_time (&unsynced, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"synced\":false,\"epoch\":0,\"iso8601\":\"\"}", s_buf);
}

TEST_CASE ("the ok response includes an optional message", "[http_server]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_ok (true, "cleared", s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"ok\":true,\"message\":\"cleared\"}", s_buf);

    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_ok (false, NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"ok\":false}", s_buf);
}

TEST_CASE ("a short buffer reports an invalid size", "[http_server]")
{
    const api_status_t status = { .system = "esp32ledctl" };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, api_json_format_status (&status, s_buf, 5));
}

TEST_CASE ("null arguments are rejected", "[http_server]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_format_status (NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_status (NULL, NULL, 0));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_led (NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_time (NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_ok (true, NULL, NULL, 0));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
