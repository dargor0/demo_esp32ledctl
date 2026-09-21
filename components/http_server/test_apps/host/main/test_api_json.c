#include "api_json.h"
#include "unity.h"

#include <stdlib.h>

static char s_buf[512];

TEST_CASE ("a single LED command is parsed without an id", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (
        ESP_OK, api_json_parse_led ("{\"r\":10,\"g\":20,\"b\":30,\"blink_ms\":500}", &cmd));
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

TEST_CASE ("an id on the single-LED body is rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"id\":1,\"r\":1,\"g\":2,\"b\":3}", &cmd));
}

TEST_CASE ("invalid single LED commands are rejected", "[http_server]")
{
    api_led_command_t cmd;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led ("{\"r\":1,\"g\":2}", &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_led ("{\"r\":256,\"g\":0,\"b\":0}", &cmd));
    TEST_ASSERT_EQUAL_INT (
        ESP_ERR_INVALID_ARG,
        api_json_parse_led ("{\"r\":0,\"g\":0,\"b\":0,\"blink_ms\":60001}", &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led ("not json", &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led (NULL, &cmd));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led ("[1,2,3]", &cmd));
}

TEST_CASE ("an array of LED commands is parsed", "[http_server]")
{
    api_led_entry_t entries[4];
    size_t count = 0;
    const char *body = "[{\"id\":1,\"r\":1,\"g\":2,\"b\":3},"
                       "{\"id\":2,\"r\":4,\"g\":5,\"b\":6,\"blink_ms\":100}]";
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_parse_led_many (body, entries, 4, &count));
    TEST_ASSERT_EQUAL_UINT (2, count);
    TEST_ASSERT_EQUAL_UINT16 (1, entries[0].id);
    TEST_ASSERT_EQUAL_INT (2, entries[0].g);
    TEST_ASSERT_EQUAL_UINT16 (2, entries[1].id);
    TEST_ASSERT_EQUAL_UINT32 (100, entries[1].blink_ms);
}

TEST_CASE ("a bad array entry rejects the whole array", "[http_server]")
{
    api_led_entry_t entries[4];
    size_t count = 0;
    TEST_ASSERT_EQUAL_INT (
        ESP_ERR_INVALID_ARG,
        api_json_parse_led_many ("[{\"r\":1,\"g\":2,\"b\":3}]", entries, 4, &count));
    TEST_ASSERT_EQUAL_INT (
        ESP_ERR_INVALID_ARG,
        api_json_parse_led_many ("[{\"id\":70000,\"r\":0,\"g\":0,\"b\":0}]", entries, 4, &count));
    TEST_ASSERT_EQUAL_INT (
        ESP_ERR_INVALID_ARG,
        api_json_parse_led_many ("{\"r\":0,\"g\":0,\"b\":0}", entries, 4, &count));
}

TEST_CASE ("array detection ignores leading whitespace", "[http_server]")
{
    TEST_ASSERT_TRUE (api_json_body_is_array ("  [ {} ]"));
    TEST_ASSERT_FALSE (api_json_body_is_array ("{\"r\":0}"));
    TEST_ASSERT_FALSE (api_json_body_is_array (NULL));
}

TEST_CASE ("an ids body is parsed", "[http_server]")
{
    uint16_t ids[8];
    size_t count = 0;
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_parse_ids ("{\"ids\":[0,2,3]}", ids, 8, &count));
    TEST_ASSERT_EQUAL_UINT (3, count);
    TEST_ASSERT_EQUAL_UINT16 (3, ids[2]);

    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_parse_ids ("{}", ids, 8, &count));
    TEST_ASSERT_EQUAL_UINT (0, count);

    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_parse_ids (NULL, ids, 8, &count));
    TEST_ASSERT_EQUAL_UINT (0, count);

    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_ids ("{\"ids\":5}", ids, 8, &count));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_parse_ids ("{\"ids\":[1,2,3,4,5,6,7,8,9]}", ids, 8, &count));
}

TEST_CASE ("a single LED is formatted as JSON", "[http_server]")
{
    const api_led_state_t led = {
        .id = 2,
        .state = "custom",
        .r = 10,
        .g = 20,
        .b = 30,
        .blink_ms = 0,
        .on = true,
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_led (&led, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"id\":2,\"state\":\"custom\",\"r\":10,\"g\":20,\"b\":30,"
                              "\"blink_ms\":0,\"on\":true}",
                              s_buf);
}

TEST_CASE ("a LED list is formatted as JSON", "[http_server]")
{
    const api_led_state_t leds[] = {
        { .id = 0, .state = "connected", .r = 0, .g = 255, .b = 0, .blink_ms = 0, .on = true },
        { .id = 1, .state = "custom", .r = 1, .g = 2, .b = 3, .blink_ms = 100, .on = false },
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_led_list (leds, 2, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING (
        "{\"leds\":[{\"id\":0,\"state\":\"connected\",\"r\":0,\"g\":255,"
        "\"b\":0,\"blink_ms\":0,\"on\":true},{\"id\":1,\"state\":\"custom\","
        "\"r\":1,\"g\":2,\"b\":3,\"blink_ms\":100,\"on\":false}],\"count\":2}",
        s_buf);
}

TEST_CASE ("the status response includes LED and button fields", "[http_server]")
{
    const api_status_t status = {
        .system = "esp32ledctl",
        .state = "connected",
        .wifi = "connected",
        .ip = "192.168.1.5",
        .uptime_s = 123,
        .free_heap = 45678,
        .led_count = 4,
        .last_button_event = "short_press",
    };
    TEST_ASSERT_EQUAL_INT (ESP_OK, api_json_format_status (&status, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_STRING ("{\"system\":\"esp32ledctl\",\"state\":\"connected\","
                              "\"wifi\":\"connected\",\"ip\":\"192.168.1.5\",\"uptime_s\":123,"
                              "\"free_heap\":45678,\"led_count\":4,"
                              "\"last_button_event\":\"short_press\"}",
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
    const api_led_state_t led = { .id = 0, .state = "connected" };
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, api_json_format_led (&led, s_buf, 5));
}

TEST_CASE ("null arguments are rejected", "[http_server]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_format_status (NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_led (NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG,
                           api_json_format_led_list (NULL, 1, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_time (NULL, s_buf, sizeof (s_buf)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_format_ok (true, NULL, NULL, 0));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_led_many (NULL, NULL, 0, NULL));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, api_json_parse_ids (NULL, NULL, 0, NULL));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
