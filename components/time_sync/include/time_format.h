/**
 * @file time_format.h
 * @brief Pure time formatting and validity helpers.
 *
 * No ESP-IDF dependency beyond esp_err_t: uses the C library time functions so
 * it can be unit-tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Epoch seconds below which the clock is considered unsynchronized. */
#define TIME_SYNC_VALID_EPOCH 1609459200LL /* 2021-01-01T00:00:00Z */

    /**
     * @brief Format an epoch as ISO-8601 UTC ("YYYY-MM-DDTHH:MM:SSZ").
     *
     * @param epoch_seconds  Seconds since the Unix epoch.
     * @param out            Destination buffer; must not be NULL.
     * @param out_size       Destination size; must be > 0.
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t time_format_iso8601 (int64_t epoch_seconds, char *out, size_t out_size);

    /**
     * @brief Format an epoch as ISO-8601 local time with offset.
     *
     * The local timezone is taken from the TZ environment variable.
     *
     * @param epoch_seconds  Seconds since the Unix epoch.
     * @param out            Destination buffer; must not be NULL.
     * @param out_size       Destination size; must be > 0.
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t time_format_iso8601_local (int64_t epoch_seconds, char *out, size_t out_size);

    /**
     * @brief Check whether an epoch is far enough in the future to be real.
     *
     * @param epoch_seconds  Seconds since the Unix epoch.
     * @return true if the clock has been synchronized.
     */
    bool time_sync_is_valid_epoch (int64_t epoch_seconds);

#ifdef __cplusplus
}
#endif
