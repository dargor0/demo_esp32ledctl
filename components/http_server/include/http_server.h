/**
 * @file http_server.h
 * @brief Local REST API and web interface.
 *
 * Serves a small web UI and a JSON REST API on the local network once WiFi is
 * connected. JSON parsing/formatting lives in the pure api_json module (see
 * api_json.h) and is unit-tested on the host.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Start the HTTP server and register the REST endpoints.
     *
     * Endpoints: GET /, /api/status, /api/time, /api/led;
     * POST /api/led, /api/led/clear, /api/wifi, /api/restart, /api/reset.
     *
     * @param port         TCP port to listen on (e.g. 80).
     * @param system_name  System name shown in the UI and status; may be NULL.
     * @return ESP_OK on success, otherwise an ESP-IDF error.
     */
    esp_err_t http_server_start (uint16_t port, const char *system_name);

#ifdef __cplusplus
}
#endif
