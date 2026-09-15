/**
 * @file storage.h
 * @brief WiFi credential persistence in Non-Volatile Storage (NVS).
 *
 * Credentials are stored as two NUL-terminated blobs (SSID and password) in a
 * dedicated NVS namespace. The public API is hardware-agnostic: the actual
 * backend is selected by the build (NVS on ESP targets) and can be replaced at
 * runtime with storage_set_backend() for host-based testing.
 *
 * Credentials are never logged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Maximum SSID length, excluding the terminator. */
#define STORAGE_SSID_MAX_LEN 32
/** @brief Maximum password length, excluding the terminator. */
#define STORAGE_PASSWORD_MAX_LEN 64

    /** @brief WiFi credentials. */
    typedef struct
    {
        char ssid[STORAGE_SSID_MAX_LEN + 1];         /**< NUL-terminated SSID. */
        char password[STORAGE_PASSWORD_MAX_LEN + 1]; /**< NUL-terminated password. */
    } storage_wifi_creds_t;

    /**
     * @brief Initialize the storage backend.
     *
     * On ESP targets this initializes NVS, recovering from a full or old version
     * partition by erasing and re-initializing it.
     *
     * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no backend is available.
     */
    esp_err_t storage_init (void);

    /**
     * @brief Validate WiFi credentials.
     *
     * The SSID must be 1..STORAGE_SSID_MAX_LEN characters. The password may be
     * empty (open network) and must not exceed STORAGE_PASSWORD_MAX_LEN.
     *
     * @param creds  Credentials to validate; must not be NULL.
     * @return ESP_OK if valid, otherwise ESP_ERR_INVALID_ARG.
     */
    esp_err_t storage_validate_wifi (const storage_wifi_creds_t *creds);

    /**
     * @brief Persist WiFi credentials.
     *
     * @param creds  Credentials to store; must pass storage_validate_wifi().
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG if invalid, or a backend error.
     */
    esp_err_t storage_save_wifi (const storage_wifi_creds_t *creds);

    /**
     * @brief Load stored WiFi credentials.
     *
     * @param creds  Destination; zeroed then filled on success.
     * @return
     *   - ESP_OK on success.
     *   - ESP_ERR_NOT_FOUND if no credentials are stored.
     *   - ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_STATE otherwise.
     */
    esp_err_t storage_load_wifi (storage_wifi_creds_t *creds);

    /**
     * @brief Erase stored WiFi credentials.
     *
     * Missing keys are treated as success, so this is idempotent.
     *
     * @return ESP_OK on success, otherwise a backend error.
     */
    esp_err_t storage_erase_wifi (void);

    /**
     * @brief Check whether WiFi credentials are stored.
     *
     * @return true if an SSID is present in storage.
     */
    bool storage_has_wifi (void);

#ifdef __cplusplus
}
#endif
