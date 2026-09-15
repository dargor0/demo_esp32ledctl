#include "mdns_name.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

static char s_out[MDNS_MAX_HOSTNAME_LEN];

TEST_CASE ("a null system name yields the default hostname", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname (NULL, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("esp32ledctl.local", s_out);
}

TEST_CASE ("an empty system name yields the default hostname", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname ("", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("esp32ledctl.local", s_out);
}

TEST_CASE ("the hostname is lowercased", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname ("MyDevice", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("mydevice.local", s_out);
}

TEST_CASE ("invalid characters become dashes", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname ("My ESP32!", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("my-esp32.local", s_out);
}

TEST_CASE ("leading and trailing dashes are trimmed", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname ("--abc", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("abc.local", s_out);

    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname ("abc---", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("abc.local", s_out);
}

TEST_CASE ("a name with no valid characters falls back to the default", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname ("@@@", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("esp32ledctl.local", s_out);
}

TEST_CASE ("a long name is truncated to the maximum label length", "[mdns_service]")
{
    char name[128];
    char expected[MDNS_MAX_HOSTNAME_LEN];
    memset (name, 'a', sizeof (name) - 1);
    name[sizeof (name) - 1] = '\0';

    memset (expected, 'a', MDNS_MAX_LABEL_LEN);
    memcpy (expected + MDNS_MAX_LABEL_LEN, ".local", sizeof (".local"));

    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_hostname (name, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING (expected, s_out);
}

TEST_CASE ("a buffer that is too small is rejected", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, mdns_build_hostname ("esp32ledctl", s_out, 5));
}

TEST_CASE ("null and zero-size hostname buffers are rejected", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, mdns_build_hostname ("net", NULL, sizeof (s_out)));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, mdns_build_hostname ("net", s_out, 0));
}

TEST_CASE ("the instance name defaults to the system name", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_instance_name (NULL, s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("esp32ledctl", s_out);

    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_instance_name ("", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("esp32ledctl", s_out);
}

TEST_CASE ("the instance name keeps the configured text", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_OK, mdns_build_instance_name ("My Device", s_out, sizeof (s_out)));
    TEST_ASSERT_EQUAL_STRING ("My Device", s_out);
}

TEST_CASE ("instance name errors are reported", "[mdns_service]")
{
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, mdns_build_instance_name ("net", NULL, 10));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, mdns_build_instance_name ("net", s_out, 0));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, mdns_build_instance_name ("long-name", s_out, 4));
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
