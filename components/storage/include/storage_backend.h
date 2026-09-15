/**
 * @file storage_backend.h
 * @brief Internal blob storage backend interface.
 *
 * This is an implementation detail of the storage component. The ESP build
 * links the NVS backend; host tests inject an in-memory fake.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Blob storage operations. */
    typedef struct
    {
        /** @brief Initialize the backend. May be NULL. */
        esp_err_t (*init) (void);
        /** @brief Store a blob under a key. */
        esp_err_t (*set_blob) (const char *key, const void *data, size_t len);
        /**
         * @brief Fetch a blob.
         *
         * Must return ESP_ERR_NOT_FOUND when the key is absent and
         * ESP_ERR_INVALID_SIZE when out_size is too small.
         */
        esp_err_t (*get_blob) (const char *key, void *out, size_t out_size, size_t *out_len);
        /** @brief Erase a key. */
        esp_err_t (*erase_key) (const char *key);
        /** @brief Check whether a key exists. */
        bool (*has_key) (const char *key);
    } storage_backend_t;

    /**
     * @brief Override the active backend (used by host tests).
     *
     * @param backend  Backend to install, or NULL to disable storage.
     */
    void storage_set_backend (const storage_backend_t *backend);

#ifdef __cplusplus
}
#endif
