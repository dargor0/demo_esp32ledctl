/**
 * @file mdns_name.h
 * @brief Pure system-name to mDNS name conversion.
 *
 * Produces a DNS-label-safe hostname ("<label>.local") and a service instance
 * name from the configured system name. No ESP-IDF dependency beyond esp_err_t.
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Default system name when none is configured. */
#define MDNS_DEFAULT_SYSTEM_NAME "esp32ledctl"
/** @brief Maximum DNS label length, per RFC 1035. */
#define MDNS_MAX_LABEL_LEN 63
/** @brief Local suffix appended to the hostname. */
#define MDNS_LOCAL_SUFFIX ".local"
/** @brief Maximum hostname length including the suffix and terminator. */
#define MDNS_MAX_HOSTNAME_LEN (MDNS_MAX_LABEL_LEN + sizeof (MDNS_LOCAL_SUFFIX))

    /**
     * @brief Build a DNS-safe hostname from the system name.
     *
     * The label is lowercased, characters outside [a-z0-9-] become '-', leading and
     * trailing '-' are removed, and the label is truncated to MDNS_MAX_LABEL_LEN.
     * An empty or NULL system name selects the default.
     *
     * @param system_name  Configured system name, may be NULL or empty.
     * @param out          Destination buffer; must not be NULL.
     * @param out_size     Destination size; must be > 0.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t mdns_build_hostname (const char *system_name, char *out, size_t out_size);

    /**
     * @brief Build the mDNS service instance name from the system name.
     *
     * An empty or NULL system name selects the default.
     *
     * @param system_name  Configured system name, may be NULL or empty.
     * @param out          Destination buffer; must not be NULL.
     * @param out_size     Destination size; must be > 0.
     * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
     */
    esp_err_t mdns_build_instance_name (const char *system_name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
