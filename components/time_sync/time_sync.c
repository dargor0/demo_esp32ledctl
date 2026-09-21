/**
 * @file time_sync.c
 * @brief SNTP glue with thread-safe sync/started flags.
 *
 * SEQUENCE
 * --------
 *   time_sync_start(): apply TZ once, then (first call only) start SNTP with the
 *     configured server and a sync callback.
 *   The SNTP callback sets `synced` under a critical section.
 *   time_sync_wait_synced(): return immediately if already synced, else block on
 *     the SNTP wait primitive and latch `synced`.
 *   is_synced() = synced && the current clock is past the validity threshold.
 *
 * The critical section guards the two booleans shared between the SNTP callback
 * task and the HTTP task.
 */
#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "time_format.h"

static const char *TAG = "time_sync";

static bool s_synced;
static bool s_started;
static portMUX_TYPE s_flags_lock = portMUX_INITIALIZER_UNLOCKED;

/** @brief Set the synchronized flag under a critical section. */
static void
time_sync_set_synced (bool synced)
{
    portENTER_CRITICAL (&s_flags_lock);
    s_synced = synced;
    portEXIT_CRITICAL (&s_flags_lock);
}

/** @brief Read the synchronized flag under a critical section. */
static bool
time_sync_get_synced (void)
{
    portENTER_CRITICAL (&s_flags_lock);
    bool synced = s_synced;
    portEXIT_CRITICAL (&s_flags_lock);
    return synced;
}

/** @brief Read/modify the started flag under a critical section. */
static bool
time_sync_take_started (void)
{
    portENTER_CRITICAL (&s_flags_lock);
    bool already = s_started;
    s_started = true;
    portEXIT_CRITICAL (&s_flags_lock);
    return already;
}

/**
 * @brief SNTP synchronization callback.
 *
 * @param tv  Updated time, unused.
 */
static void
time_sync_on_sync (struct timeval *tv)
{
    (void)tv;
    time_sync_set_synced (true);
    ESP_LOGI (TAG, "time synchronized");
}

esp_err_t
time_sync_start (const char *server, const char *timezone)
{
    if (timezone != NULL)
        {
            setenv ("TZ", timezone, 1);
            tzset ();
        }
    if (time_sync_take_started ())
        {
            return ESP_OK;
        }

    esp_sntp_config_t config
        = ESP_NETIF_SNTP_DEFAULT_CONFIG (server != NULL ? server : TIME_SYNC_DEFAULT_SERVER);
    config.sync_cb = time_sync_on_sync;

    esp_err_t err = esp_netif_sntp_init (&config);
    if (err != ESP_OK)
        {
            return err;
        }
    return ESP_OK;
}

esp_err_t
time_sync_wait_synced (uint32_t timeout_ms)
{
    if (time_sync_get_synced ())
        {
            return ESP_OK;
        }
    esp_err_t err = esp_netif_sntp_sync_wait (pdMS_TO_TICKS (timeout_ms));
    if (err == ESP_OK)
        {
            time_sync_set_synced (true);
        }
    return err;
}

bool
time_sync_is_synced (void)
{
    return time_sync_get_synced () && time_sync_is_valid_epoch (time_sync_now ());
}

int64_t
time_sync_now (void)
{
    time_t now = 0;
    time (&now);
    return (int64_t)now;
}

esp_err_t
time_sync_get (char *out, size_t out_size)
{
    return time_format_iso8601 (time_sync_now (), out, out_size);
}

esp_err_t
time_sync_get_local (char *out, size_t out_size)
{
    return time_format_iso8601_local (time_sync_now (), out, out_size);
}
