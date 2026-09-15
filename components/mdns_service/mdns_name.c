#include "mdns_name.h"

#include <string.h>

/**
 * @brief Normalize a single character for use in a DNS label.
 *
 * Uppercase letters are lowercased; characters outside [a-z0-9-] become '-'.
 *
 * @param c  Input character.
 * @return The normalized character.
 */
static char
mdns_normalize_char (char c)
{
    if (c >= 'A' && c <= 'Z')
        {
            return (char)(c - 'A' + 'a');
        }
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
        {
            return c;
        }
    return '-';
}

/**
 * @brief Sanitize a system name into a DNS label.
 *
 * @param src  Source string.
 * @param out  Destination label buffer.
 * @param max  Maximum label length.
 * @return The label length.
 */
static size_t
mdns_sanitize (const char *src, char *out, size_t max)
{
    size_t length = 0;

    for (size_t i = 0; src[i] != '\0' && length < max; i++)
        {
            char c = mdns_normalize_char (src[i]);
            if (c == '-' && length == 0)
                {
                    continue;
                }
            out[length++] = c;
        }

    while (length > 0 && out[length - 1] == '-')
        {
            length--;
        }
    out[length] = '\0';
    return length;
}

esp_err_t
mdns_build_hostname (const char *system_name, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    out[0] = '\0';

    const char *source = MDNS_DEFAULT_SYSTEM_NAME;
    if (system_name != NULL && system_name[0] != '\0')
        {
            source = system_name;
        }

    char label[MDNS_MAX_LABEL_LEN + 1];
    size_t length = mdns_sanitize (source, label, MDNS_MAX_LABEL_LEN);
    if (length == 0)
        {
            length = mdns_sanitize (MDNS_DEFAULT_SYSTEM_NAME, label, MDNS_MAX_LABEL_LEN);
        }

    size_t needed = length + sizeof (MDNS_LOCAL_SUFFIX);
    if (out_size < needed)
        {
            return ESP_ERR_INVALID_SIZE;
        }

    memcpy (out, label, length);
    memcpy (out + length, MDNS_LOCAL_SUFFIX, sizeof (MDNS_LOCAL_SUFFIX));
    return ESP_OK;
}

esp_err_t
mdns_build_instance_name (const char *system_name, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
    out[0] = '\0';

    const char *source = MDNS_DEFAULT_SYSTEM_NAME;
    if (system_name != NULL && system_name[0] != '\0')
        {
            source = system_name;
        }

    if (strlen (source) >= out_size)
        {
            return ESP_ERR_INVALID_SIZE;
        }
    strncpy (out, source, out_size - 1);
    out[out_size - 1] = '\0';
    return ESP_OK;
}
