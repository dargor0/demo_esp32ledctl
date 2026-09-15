#include "api_json.h"

#include <string.h>

#include "cJSON.h"

/**
 * @brief Read a 0-255 channel by name.
 *
 * @param root  Parsed JSON object.
 * @param name  Member name.
 * @param out   Receives the value.
 * @return true when the member exists and is in range.
 */
static bool
api_json_read_channel (const cJSON *root, const char *name, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive (root, name);
    if (!cJSON_IsNumber (item))
        {
            return false;
        }
    int value = item->valueint;
    if (value < 0 || value > 255)
        {
            return false;
        }
    *out = value;
    return true;
}

/**
 * @brief Read the optional blink_ms member.
 *
 * @param root  Parsed JSON object.
 * @param out   Receives the value (0 when absent).
 * @return true when the member is absent or a valid 0..API_JSON_MAX_BLINK_MS.
 */
static bool
api_json_read_blink (const cJSON *root, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive (root, "blink_ms");
    if (item == NULL)
        {
            *out = 0;
            return true;
        }
    if (!cJSON_IsNumber (item))
        {
            return false;
        }
    double value = item->valuedouble;
    if (value < 0 || value > API_JSON_MAX_BLINK_MS)
        {
            return false;
        }
    *out = (uint32_t)value;
    return true;
}

/**
 * @brief Serialize a cJSON tree into a caller buffer and free it.
 *
 * @param root      Tree to serialize and delete; may be NULL.
 * @param out       Destination buffer.
 * @param out_size  Destination size.
 * @return ESP_OK, ESP_ERR_NO_MEM or ESP_ERR_INVALID_SIZE.
 */
static esp_err_t
api_json_emit (cJSON *root, char *out, size_t out_size)
{
    if (root == NULL)
        {
            return ESP_ERR_NO_MEM;
        }

    char *text = cJSON_PrintUnformatted (root);
    cJSON_Delete (root);
    if (text == NULL)
        {
            return ESP_ERR_NO_MEM;
        }

    size_t length = strlen (text);
    esp_err_t err = ESP_OK;
    if (length + 1 > out_size)
        {
            err = ESP_ERR_INVALID_SIZE;
        }
    else
        {
            memcpy (out, text, length + 1);
        }
    cJSON_free (text);
    return err;
}

esp_err_t
api_json_parse_led (const char *body, api_led_command_t *out)
{
    if (body == NULL || out == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_Parse (body);
    if (root == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    api_led_command_t command;
    bool valid = api_json_read_channel (root, "r", &command.r)
                 && api_json_read_channel (root, "g", &command.g)
                 && api_json_read_channel (root, "b", &command.b)
                 && api_json_read_blink (root, &command.blink_ms);
    cJSON_Delete (root);

    if (!valid)
        {
            return ESP_ERR_INVALID_ARG;
        }
    *out = command;
    return ESP_OK;
}

esp_err_t
api_json_format_status (const api_status_t *status, char *out, size_t out_size)
{
    if (status == NULL || out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_CreateObject ();
    if (root == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    cJSON_AddStringToObject (root, "system", status->system != NULL ? status->system : "");
    cJSON_AddStringToObject (root, "state", status->state != NULL ? status->state : "");
    cJSON_AddStringToObject (root, "wifi", status->wifi != NULL ? status->wifi : "");
    cJSON_AddStringToObject (root, "ip", status->ip != NULL ? status->ip : "");
    cJSON_AddNumberToObject (root, "uptime_s", (double)status->uptime_s);
    cJSON_AddNumberToObject (root, "free_heap", (double)status->free_heap);
    return api_json_emit (root, out, out_size);
}

esp_err_t
api_json_format_led (const api_led_state_t *led, char *out, size_t out_size)
{
    if (led == NULL || out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_CreateObject ();
    if (root == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    cJSON_AddStringToObject (root, "state", led->state != NULL ? led->state : "");
    cJSON_AddNumberToObject (root, "r", led->r);
    cJSON_AddNumberToObject (root, "g", led->g);
    cJSON_AddNumberToObject (root, "b", led->b);
    cJSON_AddNumberToObject (root, "blink_ms", (double)led->blink_ms);
    cJSON_AddBoolToObject (root, "on", led->on);
    return api_json_emit (root, out, out_size);
}

esp_err_t
api_json_format_time (const api_time_t *time, char *out, size_t out_size)
{
    if (time == NULL || out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_CreateObject ();
    if (root == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    cJSON_AddBoolToObject (root, "synced", time->synced);
    cJSON_AddNumberToObject (root, "epoch", (double)time->epoch);
    cJSON_AddStringToObject (root, "iso8601", time->iso8601 != NULL ? time->iso8601 : "");
    return api_json_emit (root, out, out_size);
}

esp_err_t
api_json_format_ok (bool ok, const char *message, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_CreateObject ();
    if (root == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    cJSON_AddBoolToObject (root, "ok", ok);
    if (message != NULL)
        {
            cJSON_AddStringToObject (root, "message", message);
        }
    return api_json_emit (root, out, out_size);
}
