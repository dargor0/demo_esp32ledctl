/**
 * @file time_sync.h
 * @brief SNTP time synchronization.
 *
 * Configures the timezone and starts SNTP after the network is up. The current
 * time can be queried as ISO-8601 (UTC or local) and as epoch seconds.
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

/** @brief NTP server used when none is supplied. */
#define TIME_SYNC_DEFAULT_SERVER "pool.ntp.org"

    /**
     * @brief Set the timezone and start SNTP.
     *
     * @param server    NTP server, or NULL for TIME_SYNC_DEFAULT_SERVER.
     * @param timezone  POSIX TZ string, or NULL to leave the timezone unchanged.
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t time_sync_start (const char *server, const char *timezone);

    /**
     * @brief Block until the time is synchronized or the timeout expires.
     *
     * @param timeout_ms  Timeout in milliseconds.
     * @return ESP_OK when synchronized, ESP_ERR_TIMEOUT on timeout.
     */
    esp_err_t time_sync_wait_synced (uint32_t timeout_ms);

    /**
     * @brief Check whether the clock is synchronized.
     *
     * @return true when synchronized to an NTP server.
     */
    bool time_sync_is_synced (void);

    /**
     * @brief Get the current time as epoch seconds.
     *
     * @return Seconds since the Unix epoch.
     */
    int64_t time_sync_now (void);

    /**
     * @brief Write the current UTC time as ISO-8601.
     *
     * @param out       Destination buffer.
     * @param out_size  Destination size.
     * @return ESP_OK on success, otherwise an error.
     */
    esp_err_t time_sync_get (char *out, size_t out_size);

    /**
     * @brief Write the current local time as ISO-8601 with offset.
     *
     * @param out       Destination buffer.
     * @param out_size  Destination size.
     * @return ESP_OK on success, otherwise an error.
     */
    esp_err_t time_sync_get_local (char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
