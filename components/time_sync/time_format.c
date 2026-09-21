/**
 * @file time_format.c
 * @brief Pure time formatting / clock-validity helpers.
 *
 * ALGORITHM
 * ---------
 *   format(epoch):
 *     1. reject NULL/zero buffers;
 *     2. convert epoch seconds to a broken-down time (gmtime_r for UTC,
 *        localtime_r for local time, the latter honoring TZ);
 *     3. render with strftime; if strftime returns 0 the buffer was too small,
 *        so wipe the partial output and report ESP_ERR_INVALID_SIZE.
 *
 *   validity(epoch): an epoch is "real" once it is past TIME_SYNC_VALID_EPOCH
 *   (2021-01-01). Before NTP sync the clock reads near 0, which is below it.
 *
 * Only libc time functions are used (no ESP-IDF/hardware), so it is host-testable.
 */
#include "time_format.h"

#include <time.h>

/** @brief Format an epoch as ISO-8601 UTC ("YYYY-MM-DDTHH:MM:SSZ"). */
esp_err_t
time_format_iso8601 (int64_t epoch_seconds, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    out[0] = '\0';

    time_t seconds = (time_t)epoch_seconds;
    struct tm utc;
    if (gmtime_r (&seconds, &utc) == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }
    if (strftime (out, out_size, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        {
            out[0] = '\0';
            return ESP_ERR_INVALID_SIZE;
        }
    return ESP_OK;
}

/** @brief Format an epoch as ISO-8601 local time with the offset. */
esp_err_t
time_format_iso8601_local (int64_t epoch_seconds, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    out[0] = '\0';

    time_t seconds = (time_t)epoch_seconds;
    struct tm local;
    if (localtime_r (&seconds, &local) == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }
    if (strftime (out, out_size, "%Y-%m-%dT%H:%M:%S%z", &local) == 0)
        {
            out[0] = '\0';
            return ESP_ERR_INVALID_SIZE;
        }
    return ESP_OK;
}

/** @brief Clock is considered valid once past TIME_SYNC_VALID_EPOCH. */
bool
time_sync_is_valid_epoch (int64_t epoch_seconds)
{
    return epoch_seconds >= TIME_SYNC_VALID_EPOCH;
}
