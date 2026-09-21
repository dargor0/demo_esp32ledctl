/**
 * @file api_json.c
 * @brief Pure cJSON parsing/formatting for the REST API.
 *
 * PARSING
 * -------
 *   parse_led(body)      : a single object WITHOUT an id -> r,g,b,blink_ms.
 *                          An `id` member is rejected (collection path = all).
 *   parse_led_many(body) : an array of {id,r,g,b,blink_ms}; every entry is
 *                          validated, any bad entry fails the whole parse.
 *   parse_ids(body)      : optional {"ids":[..]}; absent/NULL -> empty list.
 *   body_is_array(body)  : cheap leading-'[' test used to pick the PUT path.
 *
 * FORMATTING
 * ----------
 *   format_led        : single object with id.
 *   format_led_list   : {"leds":[...],"count":N}.
 *   format_status     : status fields incl. led_count and last_button_event.
 *   format_time / ok  : time and generic ok responses.
 *
 * All output is built with cJSON and emitted into a bounded caller buffer;
 * ESP_ERR_INVALID_SIZE is returned when the buffer is too small.
 */
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
 * @brief Parse a command object into r,g,b,blink_ms (id not required).
 *
 * @return ESP_OK, or ESP_ERR_INVALID_ARG.
 */
static esp_err_t
api_json_parse_single (const cJSON *root, api_led_command_t *out)
{
    api_led_command_t command;
    bool valid = api_json_read_channel (root, "r", &command.r)
                 && api_json_read_channel (root, "g", &command.g)
                 && api_json_read_channel (root, "b", &command.b)
                 && api_json_read_blink (root, &command.blink_ms);
    if (!valid)
        {
            return ESP_ERR_INVALID_ARG;
        }
    *out = command;
    return ESP_OK;
}

/**
 * @brief Parse one array entry: id + the channels.
 *
 * @return true on success.
 */
static bool
api_json_parse_entry (const cJSON *item, api_led_entry_t *out)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive (item, "id");
    if (!cJSON_IsNumber (id) || id->valuedouble < 0 || id->valuedouble > 65535)
        {
            return false;
        }
    api_led_command_t command;
    if (api_json_parse_single (item, &command) != ESP_OK)
        {
            return false;
        }
    out->id = (uint16_t)id->valuedouble;
    out->r = command.r;
    out->g = command.g;
    out->b = command.b;
    out->blink_ms = command.blink_ms;
    return true;
}

/**
 * @brief Parse every entry of a JSON array.
 *
 * @return true on success.
 */
static bool
api_json_parse_entries (const cJSON *root, api_led_entry_t *out, size_t max_entries, size_t *count)
{
    int size = cJSON_GetArraySize (root);
    size_t n = 0;
    for (int i = 0; i < size; i++)
        {
            if (n >= max_entries)
                {
                    return false;
                }
            if (!api_json_parse_entry (cJSON_GetArrayItem (root, i), &out[n]))
                {
                    return false;
                }
            n++;
        }
    *count = n;
    return true;
}

/**
 * @brief Serialize a cJSON tree into a caller buffer and free it.
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

/**
 * @brief Add one LED's fields to an array.
 *
 * @return true on success.
 */
static bool
api_json_add_led (cJSON *array, const api_led_state_t *led)
{
    cJSON *item = cJSON_CreateObject ();
    if (item == NULL)
        {
            return false;
        }
    cJSON_AddNumberToObject (item, "id", (double)led->id);
    cJSON_AddStringToObject (item, "state", led->state != NULL ? led->state : "");
    cJSON_AddNumberToObject (item, "r", led->r);
    cJSON_AddNumberToObject (item, "g", led->g);
    cJSON_AddNumberToObject (item, "b", led->b);
    cJSON_AddNumberToObject (item, "blink_ms", (double)led->blink_ms);
    cJSON_AddBoolToObject (item, "on", led->on);
    cJSON_AddItemToArray (array, item);
    return true;
}

bool
api_json_body_is_array (const char *body)
{
    if (body == NULL)
        {
            return false;
        }
    size_t i = 0;
    while (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')
        {
            i++;
        }
    return body[i] == '[';
}

esp_err_t
api_json_parse_led (const char *body, api_led_command_t *out)
{
    if (body == NULL || out == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_Parse (body);
    if (root == NULL || cJSON_IsArray (root))
        {
            cJSON_Delete (root);
            return ESP_ERR_INVALID_ARG;
        }

    bool has_id = cJSON_GetObjectItemCaseSensitive (root, "id") != NULL;
    esp_err_t err = has_id ? ESP_ERR_INVALID_ARG : api_json_parse_single (root, out);
    cJSON_Delete (root);
    return err;
}

esp_err_t
api_json_parse_led_many (const char *body, api_led_entry_t *out, size_t max_entries,
                         size_t *out_count)
{
    if (body == NULL || out == NULL || out_count == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_Parse (body);
    if (root == NULL || !cJSON_IsArray (root))
        {
            cJSON_Delete (root);
            return ESP_ERR_INVALID_ARG;
        }

    bool valid = api_json_parse_entries (root, out, max_entries, out_count);
    cJSON_Delete (root);
    return valid ? ESP_OK : ESP_ERR_INVALID_ARG;
}

/**
 * @brief Parse a JSON array of numeric ids.
 *
 * @return true on success.
 */
static bool
api_json_parse_ids_array (const cJSON *array, uint16_t *out, size_t max_entries, size_t *count)
{
    int size = cJSON_GetArraySize (array);
    if ((size_t)size > max_entries)
        {
            return false;
        }
    for (int i = 0; i < size; i++)
        {
            const cJSON *item = cJSON_GetArrayItem (array, i);
            if (!cJSON_IsNumber (item) || item->valuedouble < 0 || item->valuedouble > 65535)
                {
                    return false;
                }
            out[i] = (uint16_t)item->valuedouble;
        }
    *count = (size_t)size;
    return true;
}

esp_err_t
api_json_parse_ids (const char *body, uint16_t *out, size_t max_entries, size_t *out_count)
{
    if (out == NULL || out_count == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }
    *out_count = 0;
    if (body == NULL)
        {
            return ESP_OK;
        }

    cJSON *root = cJSON_Parse (body);
    if (root == NULL)
        {
            return ESP_ERR_INVALID_ARG;
        }
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive (root, "ids");
    if (ids == NULL)
        {
            cJSON_Delete (root);
            return ESP_OK;
        }
    if (!cJSON_IsArray (ids))
        {
            cJSON_Delete (root);
            return ESP_ERR_INVALID_ARG;
        }
    bool valid = api_json_parse_ids_array (ids, out, max_entries, out_count);
    cJSON_Delete (root);
    return valid ? ESP_OK : ESP_ERR_INVALID_ARG;
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
    cJSON_AddNumberToObject (root, "id", (double)led->id);
    cJSON_AddStringToObject (root, "state", led->state != NULL ? led->state : "");
    cJSON_AddNumberToObject (root, "r", led->r);
    cJSON_AddNumberToObject (root, "g", led->g);
    cJSON_AddNumberToObject (root, "b", led->b);
    cJSON_AddNumberToObject (root, "blink_ms", (double)led->blink_ms);
    cJSON_AddBoolToObject (root, "on", led->on);
    return api_json_emit (root, out, out_size);
}

esp_err_t
api_json_format_led_list (const api_led_state_t *leds, size_t count, char *out, size_t out_size)
{
    if (leds == NULL || out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

    cJSON *root = cJSON_CreateObject ();
    if (root == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    cJSON *array = cJSON_AddArrayToObject (root, "leds");
    cJSON_AddNumberToObject (root, "count", (double)count);
    if (array == NULL)
        {
            cJSON_Delete (root);
            return ESP_ERR_NO_MEM;
        }

    for (size_t i = 0; i < count; i++)
        {
            if (!api_json_add_led (array, &leds[i]))
                {
                    cJSON_Delete (root);
                    return ESP_ERR_NO_MEM;
                }
        }
    return api_json_emit (root, out, out_size);
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
    cJSON_AddNumberToObject (root, "led_count", (double)status->led_count);
    cJSON_AddStringToObject (root, "last_button_event",
                             status->last_button_event != NULL ? status->last_button_event : "");
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
