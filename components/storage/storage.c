#include "storage.h"

#include <string.h>

#include "storage_backend.h"

/** @brief Key under which the SSID blob is stored. */
#define STORAGE_KEY_SSID "ssid"
/** @brief Key under which the password blob is stored. */
#define STORAGE_KEY_PASSWORD "password"

#ifdef STORAGE_HAVE_NVS
extern const storage_backend_t storage_nvs_backend;
static const storage_backend_t *s_backend = &storage_nvs_backend;
#else
static const storage_backend_t *s_backend = NULL;
#endif

void
storage_set_backend (const storage_backend_t *backend)
{
    s_backend = backend;
}

esp_err_t
storage_init (void)
{
    if (s_backend == NULL)
        {
            return ESP_ERR_INVALID_STATE;
        }
    return s_backend->init != NULL ? s_backend->init () : ESP_OK;
}

esp_err_t
storage_validate_wifi (const storage_wifi_creds_t *creds)
{
    if (creds == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    size_t ssid_len = strnlen (creds->ssid, STORAGE_SSID_MAX_LEN + 1);
    if (ssid_len == 0 || ssid_len > STORAGE_SSID_MAX_LEN)
        {
            return ESP_ERR_INVALID_ARG;
        }

    size_t password_len = strnlen (creds->password, STORAGE_PASSWORD_MAX_LEN + 1);
    if (password_len > STORAGE_PASSWORD_MAX_LEN)
        {
            return ESP_ERR_INVALID_ARG;
        }

    return ESP_OK;
}

esp_err_t
storage_save_wifi (const storage_wifi_creds_t *creds)
{
    esp_err_t err = storage_validate_wifi (creds);
    if (err != ESP_OK)
        {
            return err;
        }
    if (s_backend == NULL || s_backend->set_blob == NULL)
        {
            return ESP_ERR_INVALID_STATE;
        }

    err = s_backend->set_blob (STORAGE_KEY_SSID, creds->ssid, strlen (creds->ssid) + 1);
    if (err != ESP_OK)
        {
            return err;
        }
    return s_backend->set_blob (STORAGE_KEY_PASSWORD, creds->password,
                                strlen (creds->password) + 1);
}

esp_err_t
storage_load_wifi (storage_wifi_creds_t *creds)
{
    if (creds == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }
    if (s_backend == NULL || s_backend->get_blob == NULL)
        {
            return ESP_ERR_INVALID_STATE;
        }

    memset (creds, 0, sizeof (*creds));

    size_t actual = 0;
    esp_err_t err
        = s_backend->get_blob (STORAGE_KEY_SSID, creds->ssid, sizeof (creds->ssid), &actual);
    if (err != ESP_OK)
        {
            return err;
        }

    err = s_backend->get_blob (STORAGE_KEY_PASSWORD, creds->password, sizeof (creds->password),
                               &actual);
    if (err != ESP_OK)
        {
            return err;
        }

    return storage_validate_wifi (creds);
}

esp_err_t
storage_erase_wifi (void)
{
    if (s_backend == NULL || s_backend->erase_key == NULL)
        {
            return ESP_ERR_INVALID_STATE;
        }

    esp_err_t err = s_backend->erase_key (STORAGE_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
        {
            return err;
        }

    err = s_backend->erase_key (STORAGE_KEY_PASSWORD);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
        {
            return err;
        }

    return ESP_OK;
}

bool
storage_has_wifi (void)
{
    if (s_backend == NULL || s_backend->has_key == NULL)
        {
            return false;
        }
    return s_backend->has_key (STORAGE_KEY_SSID);
}
