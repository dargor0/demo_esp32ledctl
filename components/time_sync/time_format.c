#include "time_format.h"

#include <time.h>

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

bool
time_sync_is_valid_epoch (int64_t epoch_seconds)
{
    return epoch_seconds >= TIME_SYNC_VALID_EPOCH;
}
