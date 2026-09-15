#include "app_wifi.h"
#include "unity.h"
#include "wifi_logic.h"

#include <stdlib.h>

static wifi_logic_t s_logic;

static void
reset (void)
{
    wifi_logic_init (&s_logic, NULL);
}

static app_wifi_state_t
apply (wifi_logic_event_t event)
{
    return wifi_logic_process (&s_logic, event, NULL);
}

TEST_CASE ("default configuration is idle and unlimited", "[app_wifi]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_IDLE, (int)s_logic.state);
    TEST_ASSERT_EQUAL_UINT32 (0, s_logic.retries);
    TEST_ASSERT_EQUAL_UINT32 (WIFI_LOGIC_DEFAULT_BASE_DELAY_MS, s_logic.base_delay_ms);
    TEST_ASSERT_EQUAL_UINT32 (WIFI_LOGIC_DEFAULT_MAX_DELAY_MS, s_logic.max_delay_ms);
    TEST_ASSERT_EQUAL_UINT32 (0, s_logic.max_retries);
}

TEST_CASE ("a custom configuration is applied", "[app_wifi]")
{
    const wifi_logic_config_t config = {
        .base_delay_ms = 250,
        .max_delay_ms = 4000,
        .max_retries = 3,
    };
    wifi_logic_init (&s_logic, &config);
    TEST_ASSERT_EQUAL_UINT32 (250, s_logic.base_delay_ms);
    TEST_ASSERT_EQUAL_UINT32 (4000, s_logic.max_delay_ms);
    TEST_ASSERT_EQUAL_UINT32 (3, s_logic.max_retries);
}

TEST_CASE ("connect requests and station start lead to connecting", "[app_wifi]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTING, (int)apply (WIFI_LOGIC_CONNECT_REQUESTED));
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTING, (int)apply (WIFI_LOGIC_STA_STARTED));
}

TEST_CASE ("getting an IP leads to connected and clears retries", "[app_wifi]")
{
    reset ();
    s_logic.retries = 5;
    uint32_t delay = 1234;
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTED,
                           (int)wifi_logic_process (&s_logic, WIFI_LOGIC_GOT_IP, &delay));
    TEST_ASSERT_EQUAL_UINT32 (0, s_logic.retries);
    TEST_ASSERT_EQUAL_UINT32 (0, delay);
}

TEST_CASE ("a disconnect schedules an exponential backoff", "[app_wifi]")
{
    reset ();
    uint32_t delay = 0;

    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTING,
                           (int)wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay));
    TEST_ASSERT_EQUAL_UINT32 (1000, delay);
    TEST_ASSERT_EQUAL_UINT32 (1, s_logic.retries);

    wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay);
    TEST_ASSERT_EQUAL_UINT32 (2000, delay);

    wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay);
    TEST_ASSERT_EQUAL_UINT32 (4000, delay);
}

TEST_CASE ("the backoff is capped at the maximum delay", "[app_wifi]")
{
    const wifi_logic_config_t config
        = { .base_delay_ms = 1000, .max_delay_ms = 3000, .max_retries = 0 };
    wifi_logic_init (&s_logic, &config);

    uint32_t delay = 0;
    for (int i = 0; i < 6; i++)
        {
            wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay);
            TEST_ASSERT_TRUE (delay <= 3000);
        }
    TEST_ASSERT_EQUAL_UINT32 (3000, delay);
}

TEST_CASE ("retries eventually give up when a maximum is set", "[app_wifi]")
{
    const wifi_logic_config_t config
        = { .base_delay_ms = 100, .max_delay_ms = 10000, .max_retries = 2 };
    wifi_logic_init (&s_logic, &config);

    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTING, (int)apply (WIFI_LOGIC_DISCONNECTED));
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTING, (int)apply (WIFI_LOGIC_DISCONNECTED));
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_ERROR, (int)apply (WIFI_LOGIC_DISCONNECTED));
}

TEST_CASE ("unlimited retries never give up", "[app_wifi]")
{
    reset ();
    for (int i = 0; i < 20; i++)
        {
            TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTING, (int)apply (WIFI_LOGIC_DISCONNECTED));
        }
}

TEST_CASE ("reconnecting after success restarts the backoff", "[app_wifi]")
{
    reset ();
    uint32_t delay = 0;
    wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay);
    wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay);
    TEST_ASSERT_EQUAL_UINT32 (2, s_logic.retries);

    wifi_logic_process (&s_logic, WIFI_LOGIC_GOT_IP, &delay);
    wifi_logic_process (&s_logic, WIFI_LOGIC_DISCONNECTED, &delay);
    TEST_ASSERT_EQUAL_UINT32 (1000, delay);
    TEST_ASSERT_EQUAL_UINT32 (1, s_logic.retries);
}

TEST_CASE ("a failure leads to the error state", "[app_wifi]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_ERROR, (int)apply (WIFI_LOGIC_FAILED));
}

TEST_CASE ("null inputs are handled safely", "[app_wifi]")
{
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_ERROR,
                           (int)wifi_logic_process (NULL, WIFI_LOGIC_GOT_IP, NULL));
    wifi_logic_init (NULL, NULL);
    reset ();
    TEST_ASSERT_EQUAL_INT (APP_WIFI_STATE_CONNECTED,
                           (int)wifi_logic_process (&s_logic, WIFI_LOGIC_GOT_IP, NULL));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
