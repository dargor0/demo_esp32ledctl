/**
 * @file mdns_service.h
 * @brief mDNS hostname and HTTP service advertisement.
 *
 * Once the network is up, the device advertises itself as
 * "<system-name>.local" and registers the HTTP service (_http._tcp) so it can
 * be reached by name on the local network.
 *
 * The system-name to hostname conversion lives in the pure mdns_name module
 * (see mdns_name.h) and is unit-tested on the host.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Start mDNS and advertise the HTTP service.
     *
     * @param system_name  System name used for the hostname and service instance;
     *                     NULL or empty selects the built-in default.
     * @param http_port    Port of the HTTP server to advertise.
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t mdns_service_start (const char *system_name, uint16_t http_port);

#ifdef __cplusplus
}
#endif
