/**
 * @file storage.c
 * @brief WiFi credential persistence with a pluggable backend.
 *
 * ALGORITHM
 * ---------
 * Credentials are two blobs (SSID and password) under a namespace. Because the
 * pair must stay consistent, every public operation takes a mutex:
 *
 *   save : validate -> lock -> set SSID -> set password -> unlock
 *   load : lock -> get SSID -> get password -> unlock -> validate
 *   erase: lock -> erase SSID -> erase password (NOT_FOUND tolerated) -> unlock
 *   has  : lock -> has_key(SSID) -> unlock
 *
 * The backend is a vtable: the ESP build installs the NVS backend, host tests
 * install an in-memory fake. The lock is a no-op on the host.
 */
#include "storage.h"

#include <string.h>

#include "storage_backend.h"

/** @brief Key under which the SSID blob is stored. */
#define STORAGE_KEY_SSID "ssid"
/** @brief Key under which the password blob is stored. */
#define STORAGE_KEY_PASSWORD "password"

/*
 * The credential operations span two backend keys (SSID + password), so they
 * are serialized by a mutex to keep the pair consistent. The lock is a no-op on
 * the host test build.
 */
#ifndef CONFIG_IDF_TARGET_LINUX
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_lock;
static void
storage_lock_init (void)
{
    if (s_lock == NULL)
        {
            s_lock = xSemaphoreCreateMutex ();
        }
}
static void
storage_lock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreTake (s_lock, portMAX_DELAY);
        }
}
static void
storage_unlock (void)
{
    if (s_lock != NULL)
        {
            xSemaphoreGive (s_lock);
        }
}
#else
static void
storage_lock_init (void)
{
}
static void
storage_lock (void)
{
}
static void
storage_unlock (void)
{
}
#endif

#ifdef STORAGE_HAVE_NVS
extern const storage_backend_t storage_nvs_backend;
static const storage_backend_t *s_backend = &storage_nvs_backend;
#else
static const storage_backend_t *s_backend = NULL;
#endif

void
storage_set_backend (const storage_backend_t *backend)
{
    storage_lock_init ();
    storage_lock ();
    s_backend = backend;
    storage_unlock ();
}

esp_err_t
storage_init (void)
{
    storage_lock_init ();
    storage_lock ();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_backend != NULL)
        {
            err = s_backend->init != NULL ? s_backend->init () : ESP_OK;
        }
    storage_unlock ();
    return err;
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

    storage_lock ();
    if (s_backend == NULL || s_backend->set_blob == NULL)
        {
            storage_unlock ();
            return ESP_ERR_INVALID_STATE;
        }

    err = s_backend->set_blob (STORAGE_KEY_SSID, creds->ssid, strlen (creds->ssid) + 1);
    if (err == ESP_OK)
        {
            err = s_backend->set_blob (STORAGE_KEY_PASSWORD, creds->password,
                                       strlen (creds->password) + 1);
        }
    storage_unlock ();
    return err;
}

esp_err_t
storage_load_wifi (storage_wifi_creds_t *creds)
{
    if (creds == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    storage_lock ();
    if (s_backend == NULL || s_backend->get_blob == NULL)
        {
            storage_unlock ();
            return ESP_ERR_INVALID_STATE;
        }

    memset (creds, 0, sizeof (*creds));
    size_t actual = 0;
    esp_err_t err
        = s_backend->get_blob (STORAGE_KEY_SSID, creds->ssid, sizeof (creds->ssid), &actual);
    if (err == ESP_OK)
        {
            err = s_backend->get_blob (STORAGE_KEY_PASSWORD, creds->password,
                                       sizeof (creds->password), &actual);
        }
    storage_unlock ();

    if (err != ESP_OK)
        {
            return err;
        }
    return storage_validate_wifi (creds);
}

esp_err_t
storage_erase_wifi (void)
{
    storage_lock ();
    if (s_backend == NULL || s_backend->erase_key == NULL)
        {
            storage_unlock ();
            return ESP_ERR_INVALID_STATE;
        }

    esp_err_t err = s_backend->erase_key (STORAGE_KEY_SSID);
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND)
        {
            err = s_backend->erase_key (STORAGE_KEY_PASSWORD);
            if (err == ESP_ERR_NOT_FOUND)
                {
                    err = ESP_OK;
                }
        }
    storage_unlock ();
    return err;
}

bool
storage_has_wifi (void)
{
    storage_lock ();
    bool present = false;
    if (s_backend != NULL && s_backend->has_key != NULL)
        {
            present = s_backend->has_key (STORAGE_KEY_SSID);
        }
    storage_unlock ();
    return present;
}
