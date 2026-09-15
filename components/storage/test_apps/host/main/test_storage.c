#include "storage.h"
#include "storage_backend.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

#define FAKE_MAX_ENTRIES 8
#define FAKE_KEY_MAX 16
#define FAKE_VALUE_MAX 128

typedef struct
{
    char key[FAKE_KEY_MAX];
    uint8_t value[FAKE_VALUE_MAX];
    size_t len;
    bool used;
} fake_entry_t;

static fake_entry_t s_entries[FAKE_MAX_ENTRIES];
static esp_err_t s_init_result;
static esp_err_t s_set_result;
static esp_err_t s_get_result;
static esp_err_t s_erase_result;
static int s_init_calls;
static int s_erase_calls;
static int s_erase_fail_at;

static fake_entry_t *
fake_find (const char *key)
{
    for (int i = 0; i < FAKE_MAX_ENTRIES; i++)
        {
            if (s_entries[i].used && strcmp (s_entries[i].key, key) == 0)
                {
                    return &s_entries[i];
                }
        }
    return NULL;
}

static fake_entry_t *
fake_alloc (const char *key)
{
    fake_entry_t *entry = fake_find (key);
    if (entry != NULL)
        {
            return entry;
        }
    for (int i = 0; i < FAKE_MAX_ENTRIES; i++)
        {
            if (!s_entries[i].used)
                {
                    s_entries[i].used = true;
                    strncpy (s_entries[i].key, key, FAKE_KEY_MAX - 1);
                    return &s_entries[i];
                }
        }
    return NULL;
}

static esp_err_t
fake_init (void)
{
    s_init_calls++;
    return s_init_result;
}

static esp_err_t
fake_set_blob (const char *key, const void *data, size_t len)
{
    if (s_set_result != ESP_OK)
        {
            return s_set_result;
        }
    if (len > FAKE_VALUE_MAX)
        {
            return ESP_ERR_INVALID_SIZE;
        }
    fake_entry_t *entry = fake_alloc (key);
    if (entry == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    memcpy (entry->value, data, len);
    entry->len = len;
    return ESP_OK;
}

static esp_err_t
fake_get_blob (const char *key, void *out, size_t out_size, size_t *out_len)
{
    if (s_get_result != ESP_OK)
        {
            return s_get_result;
        }
    fake_entry_t *entry = fake_find (key);
    if (entry == NULL)
        {
            return ESP_ERR_NOT_FOUND;
        }
    if (entry->len > out_size)
        {
            return ESP_ERR_INVALID_SIZE;
        }
    memcpy (out, entry->value, entry->len);
    if (out_len != NULL)
        {
            *out_len = entry->len;
        }
    return ESP_OK;
}

static esp_err_t
fake_erase_key (const char *key)
{
    int call = s_erase_calls++;
    if (s_erase_result != ESP_OK)
        {
            return s_erase_result;
        }
    if (call == s_erase_fail_at)
        {
            return ESP_FAIL;
        }
    fake_entry_t *entry = fake_find (key);
    if (entry == NULL)
        {
            return ESP_ERR_NOT_FOUND;
        }
    entry->used = false;
    return ESP_OK;
}

static bool
fake_has_key (const char *key)
{
    return fake_find (key) != NULL;
}

static const storage_backend_t s_fake_backend = {
    .init = fake_init,
    .set_blob = fake_set_blob,
    .get_blob = fake_get_blob,
    .erase_key = fake_erase_key,
    .has_key = fake_has_key,
};

static const storage_backend_t s_empty_backend = { 0 };

static void
reset (void)
{
    memset (s_entries, 0, sizeof (s_entries));
    s_init_result = ESP_OK;
    s_set_result = ESP_OK;
    s_get_result = ESP_OK;
    s_erase_result = ESP_OK;
    s_init_calls = 0;
    s_erase_calls = 0;
    s_erase_fail_at = -1;
    storage_set_backend (&s_fake_backend);
}

static storage_wifi_creds_t
make_creds (const char *ssid, const char *password)
{
    storage_wifi_creds_t creds;
    memset (&creds, 0, sizeof (creds));
    strncpy (creds.ssid, ssid, STORAGE_SSID_MAX_LEN);
    strncpy (creds.password, password, STORAGE_PASSWORD_MAX_LEN);
    return creds;
}

TEST_CASE ("valid credentials pass validation", "[storage]")
{
    reset ();
    storage_wifi_creds_t creds = make_creds ("MyNetwork", "secret123");
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_validate_wifi (&creds));
}

TEST_CASE ("null credentials are rejected", "[storage]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, storage_validate_wifi (NULL));
}

TEST_CASE ("an empty SSID is rejected", "[storage]")
{
    reset ();
    storage_wifi_creds_t creds = make_creds ("", "secret");
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, storage_validate_wifi (&creds));
}

TEST_CASE ("an SSID of maximum length is accepted", "[storage]")
{
    reset ();
    char ssid[STORAGE_SSID_MAX_LEN + 1];
    memset (ssid, 'a', STORAGE_SSID_MAX_LEN);
    ssid[STORAGE_SSID_MAX_LEN] = '\0';
    storage_wifi_creds_t creds = make_creds (ssid, "");
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_validate_wifi (&creds));
}

TEST_CASE ("an over-long SSID is rejected", "[storage]")
{
    reset ();
    storage_wifi_creds_t creds;
    memset (&creds, 'a', sizeof (creds));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, storage_validate_wifi (&creds));
}

TEST_CASE ("an over-long password is rejected", "[storage]")
{
    reset ();
    storage_wifi_creds_t creds = make_creds ("net", "");
    memset (creds.password, 'a', sizeof (creds.password));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, storage_validate_wifi (&creds));
}

TEST_CASE ("credentials survive a save and load round trip", "[storage]")
{
    reset ();
    storage_wifi_creds_t saved = make_creds ("HomeNet", "p@ssw0rd");
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_save_wifi (&saved));
    TEST_ASSERT_TRUE (storage_has_wifi ());

    storage_wifi_creds_t loaded;
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_load_wifi (&loaded));
    TEST_ASSERT_EQUAL_STRING ("HomeNet", loaded.ssid);
    TEST_ASSERT_EQUAL_STRING ("p@ssw0rd", loaded.password);
}

TEST_CASE ("an open network with an empty password is allowed", "[storage]")
{
    reset ();
    storage_wifi_creds_t saved = make_creds ("OpenNet", "");
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_save_wifi (&saved));

    storage_wifi_creds_t loaded;
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_load_wifi (&loaded));
    TEST_ASSERT_EQUAL_STRING ("OpenNet", loaded.ssid);
    TEST_ASSERT_EQUAL_STRING ("", loaded.password);
}

TEST_CASE ("loading with nothing stored reports not found", "[storage]")
{
    reset ();
    storage_wifi_creds_t loaded;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_NOT_FOUND, storage_load_wifi (&loaded));
}

TEST_CASE ("loading into null is rejected", "[storage]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, storage_load_wifi (NULL));
}

TEST_CASE ("erase removes credentials and is idempotent", "[storage]")
{
    reset ();
    storage_wifi_creds_t saved = make_creds ("net", "pass");
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_save_wifi (&saved));

    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_erase_wifi ());
    TEST_ASSERT_FALSE (storage_has_wifi ());
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_erase_wifi ());
}

TEST_CASE ("invalid credentials are not stored", "[storage]")
{
    reset ();
    storage_wifi_creds_t saved = make_creds ("", "pass");
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_ARG, storage_save_wifi (&saved));
    TEST_ASSERT_FALSE (storage_has_wifi ());
}

TEST_CASE ("init calls the backend and propagates its result", "[storage]")
{
    reset ();
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_init ());
    TEST_ASSERT_EQUAL_INT (1, s_init_calls);

    s_init_result = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT (ESP_FAIL, storage_init ());
}

TEST_CASE ("save propagates backend errors", "[storage]")
{
    reset ();
    s_set_result = ESP_FAIL;
    storage_wifi_creds_t saved = make_creds ("net", "pass");
    TEST_ASSERT_EQUAL_INT (ESP_FAIL, storage_save_wifi (&saved));
}

TEST_CASE ("load propagates backend errors", "[storage]")
{
    reset ();
    s_get_result = ESP_ERR_INVALID_SIZE;
    storage_wifi_creds_t loaded;
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_SIZE, storage_load_wifi (&loaded));
}

TEST_CASE ("erase propagates backend errors", "[storage]")
{
    reset ();
    s_erase_result = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT (ESP_FAIL, storage_erase_wifi ());
}

TEST_CASE ("erase propagates a password erase failure", "[storage]")
{
    reset ();
    storage_wifi_creds_t saved = make_creds ("net", "pass");
    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_save_wifi (&saved));
    s_erase_fail_at = 1;
    TEST_ASSERT_EQUAL_INT (ESP_FAIL, storage_erase_wifi ());
}

TEST_CASE ("operations without a backend report invalid state", "[storage]")
{
    reset ();
    storage_set_backend (NULL);
    storage_wifi_creds_t creds = make_creds ("net", "pass");

    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_init ());
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_save_wifi (&creds));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_load_wifi (&creds));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_erase_wifi ());
    TEST_ASSERT_FALSE (storage_has_wifi ());
}

TEST_CASE ("a backend with missing hooks is handled", "[storage]")
{
    storage_set_backend (&s_empty_backend);
    storage_wifi_creds_t creds = make_creds ("net", "pass");

    TEST_ASSERT_EQUAL_INT (ESP_OK, storage_init ());
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_save_wifi (&creds));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_load_wifi (&creds));
    TEST_ASSERT_EQUAL_INT (ESP_ERR_INVALID_STATE, storage_erase_wifi ());
    TEST_ASSERT_FALSE (storage_has_wifi ());
}

void
app_main (void)
{
    unity_run_all_tests ();
    exit (Unity.TestFailures == 0 ? 0 : 1);
}
