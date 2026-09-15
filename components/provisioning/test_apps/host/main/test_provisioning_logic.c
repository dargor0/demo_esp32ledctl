#include "provisioning.h"
#include "provisioning_logic.h"
#include "unity.h"

#include <stdlib.h>

static char s_name[PROVISIONING_MAX_NAME_LEN];
static provisioning_logic_t s_logic;

TEST_CASE ("the default device name is derived from the default system name", "[provisioning]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, provisioning_build_name (NULL, s_name, sizeof (s_name)));
    TEST_ASSERT_EQUAL_STRING ("PROV_esp32ledctl", s_name);

    TEST_ASSERT_EQUAL_INT (ESP_OK, provisioning_build_name ("", s_name, sizeof (s_name)));
    TEST_ASSERT_EQUAL_STRING ("PROV_esp32ledctl", s_name);
}

TEST_CASE ("the device name uses the configured system name", "[provisioning]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, provisioning_build_name ("MyDevice", s_name, sizeof (s_name)));
    TEST_ASSERT_EQUAL_STRING ("PROV_MyDevice", s_name);
}

TEST_CASE ("the device name is truncated to the buffer", "[provisioning]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, provisioning_build_name ("MyDevice", s_name, 10));
    TEST_ASSERT_EQUAL_STRING ("PROV_MyDe", s_name);

    TEST_ASSERT_EQUAL_INT (ESP_OK, provisioning_build_name ("MyDevice", s_name, 6));
    TEST_ASSERT_EQUAL_STRING ("PROV_", s_name);
}

TEST_CASE ("a buffer smaller than the prefix is rejected", "[provisioning]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE,
                           provisioning_build_name ("net", s_name, sizeof ("PROV_") - 1));
}

TEST_CASE ("null and zero-size name buffers are rejected", "[provisioning]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, provisioning_build_name ("net", NULL, 10));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, provisioning_build_name ("net", s_name, 0));
}

TEST_CASE ("init selects provisioning only when unprovisioned", "[provisioning]")
{
    provisioning_logic_init (&s_logic, false);
    TEST_ASSERT_EQUAL_INT (PROV_STATE_PROVISIONING, (int)s_logic.state);

    provisioning_logic_init (&s_logic, true);
    TEST_ASSERT_EQUAL_INT (PROV_STATE_IDLE, (int)s_logic.state);
}

TEST_CASE ("events drive the provisioning state machine", "[provisioning]")
{
    provisioning_logic_init (&s_logic, false);

    TEST_ASSERT_EQUAL_INT (PROV_STATE_PROVISIONING,
                           (int)provisioning_logic_process (&s_logic, PROV_EVENT_STARTED));
    TEST_ASSERT_EQUAL_INT (PROV_STATE_CONNECTING,
                           (int)provisioning_logic_process (&s_logic, PROV_EVENT_CREDENTIALS));
    TEST_ASSERT_EQUAL_INT (PROV_STATE_CONNECTED,
                           (int)provisioning_logic_process (&s_logic, PROV_EVENT_CONNECTED));
    TEST_ASSERT_EQUAL_INT (PROV_STATE_FAILED,
                           (int)provisioning_logic_process (&s_logic, PROV_EVENT_FAILED));
    TEST_ASSERT_EQUAL_INT (PROV_STATE_PROVISIONING,
                           (int)provisioning_logic_process (&s_logic, PROV_EVENT_RESET));
}

TEST_CASE ("null state machines are handled safely", "[provisioning]")
{
    provisioning_logic_init (NULL, false);
    TEST_ASSERT_EQUAL_INT (PROV_STATE_FAILED,
                           (int)provisioning_logic_process (NULL, PROV_EVENT_STARTED));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
