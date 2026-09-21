/**
 * @file storage_nvs.c
 * @brief NVS-backed implementation of the storage backend vtable.
 *
 * SEQUENCE
 * --------
 * Each operation opens the NVS namespace, performs one primitive, commits when
 * writing and closes the handle. This keeps the handle lifetime local to the
 * call (no long-lived handle/lock). init() recovers a full or stale partition
 * by erasing and re-initializing it.
 *
 * Error mapping: NVS "not found" -> ESP_ERR_NOT_FOUND, NVS "invalid length"
 * -> ESP_ERR_INVALID_SIZE, so the core logic is backend-agnostic.
 */
#include "storage_backend.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

/** @brief NVS namespace holding the WiFi credential blobs. */
#define STORAGE_NVS_NAMESPACE "app_wifi"

/**
 * @brief Initialize NVS, recovering from a full or stale partition.
 *
 * @return ESP_OK on success.
 */
static esp_err_t
nvs_backend_init (void)
{
    esp_err_t err = nvs_flash_init ();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            esp_err_t erase_err = nvs_flash_erase ();
            if (erase_err != ESP_OK)
                {
                    return erase_err;
                }
            err = nvs_flash_init ();
        }
    return err;
}

/**
 * @brief Store a blob in NVS.
 *
 * @param key   Key name.
 * @param data  Blob data.
 * @param len   Blob length in bytes.
 * @return ESP_OK on success, otherwise an NVS error.
 */
static esp_err_t
nvs_backend_set_blob (const char *key, const void *data, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open (STORAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        {
            return err;
        }
    err = nvs_set_blob (handle, key, data, len);
    if (err == ESP_OK)
        {
            err = nvs_commit (handle);
        }
    nvs_close (handle);
    return err;
}

/**
 * @brief Fetch a blob from NVS.
 *
 * @param key       Key name.
 * @param out       Destination buffer.
 * @param out_size  Destination size in bytes.
 * @param out_len   Receives the blob length, may be NULL.
 * @return ESP_OK, ESP_ERR_NOT_FOUND or ESP_ERR_INVALID_SIZE.
 */
static esp_err_t
nvs_backend_get_blob (const char *key, void *out, size_t out_size, size_t *out_len)
{
    nvs_handle_t handle;
    if (nvs_open (STORAGE_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        {
            return ESP_ERR_NOT_FOUND;
        }

    size_t len = out_size;
    esp_err_t err = nvs_get_blob (handle, key, out, &len);
    nvs_close (handle);

    if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            return ESP_ERR_NOT_FOUND;
        }
    if (err == ESP_ERR_NVS_INVALID_LENGTH)
        {
            return ESP_ERR_INVALID_SIZE;
        }
    if (err != ESP_OK)
        {
            return err;
        }
    if (out_len != NULL)
        {
            *out_len = len;
        }
    return ESP_OK;
}

/**
 * @brief Erase a key from NVS.
 *
 * A missing namespace or key is treated as success to keep erase idempotent.
 *
 * @param key  Key name.
 * @return ESP_OK on success, otherwise an NVS error.
 */
static esp_err_t
nvs_backend_erase_key (const char *key)
{
    nvs_handle_t handle;
    if (nvs_open (STORAGE_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        {
            return ESP_OK;
        }

    esp_err_t err = nvs_erase_key (handle, key);
    if (err == ESP_OK)
        {
            err = nvs_commit (handle);
        }
    nvs_close (handle);

    if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            return ESP_ERR_NOT_FOUND;
        }
    return err;
}

/**
 * @brief Check whether a key exists in NVS.
 *
 * @param key  Key name.
 * @return true if the key holds a blob.
 */
static bool
nvs_backend_has_key (const char *key)
{
    nvs_handle_t handle;
    if (nvs_open (STORAGE_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        {
            return false;
        }

    size_t len = 0;
    esp_err_t err = nvs_get_blob (handle, key, NULL, &len);
    nvs_close (handle);
    return err == ESP_OK;
}

/** @brief NVS-backed storage backend. */
const storage_backend_t storage_nvs_backend = {
    .init = nvs_backend_init,
    .set_blob = nvs_backend_set_blob,
    .get_blob = nvs_backend_get_blob,
    .erase_key = nvs_backend_erase_key,
    .has_key = nvs_backend_has_key,
};
