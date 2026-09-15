#include "http_server.h"

#include <string.h>

#include "api_json.h"
#include "app_wifi.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "led_rgb.h"
#include "storage.h"
#include "time_sync.h"

/** @brief System name buffer size. */
#define HTTP_SERVER_NAME_MAX 32
/** @brief JSON response buffer size. */
#define HTTP_SERVER_JSON_MAX 256
/** @brief Request body buffer size. */
#define HTTP_SERVER_BODY_MAX 128
/** @brief Web UI HTML buffer size. */
#define HTTP_SERVER_HTML_MAX 768

static const char *TAG = "http_server";

static char s_system_name[HTTP_SERVER_NAME_MAX];
static char s_index_html[HTTP_SERVER_HTML_MAX];

/**
 * @brief Map an LED state to its API name.
 *
 * @param state  LED state.
 * @return Static string name.
 */
static const char *
http_server_led_state_name (led_rgb_state_t state)
{
    switch (state)
        {
        case LED_RGB_STATE_PROVISIONING:
            return "provisioning";
        case LED_RGB_STATE_CONNECTING:
            return "connecting";
        case LED_RGB_STATE_CONNECTED:
            return "connected";
        case LED_RGB_STATE_ERROR:
            return "error";
        case LED_RGB_STATE_CUSTOM:
            return "custom";
        default:
            return "unknown";
        }
}

/**
 * @brief Map a WiFi state to its API name.
 *
 * @param state  WiFi state.
 * @return Static string name.
 */
static const char *
http_server_wifi_state_name (app_wifi_state_t state)
{
    switch (state)
        {
        case APP_WIFI_STATE_IDLE:
            return "idle";
        case APP_WIFI_STATE_CONNECTING:
            return "connecting";
        case APP_WIFI_STATE_CONNECTED:
            return "connected";
        case APP_WIFI_STATE_ERROR:
            return "error";
        default:
            return "unknown";
        }
}

/**
 * @brief Get the station IPv4 address as a string.
 *
 * @return Static string, or "" when unavailable.
 */
static const char *
http_server_ip_string (void)
{
    static char ip[16];
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey ("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (netif == NULL || esp_netif_get_ip_info (netif, &info) != ESP_OK)
        {
            return "";
        }
    snprintf (ip, sizeof (ip), IPSTR, IP2STR (&info.ip));
    return ip;
}

/**
 * @brief Send a JSON error response.
 *
 * @param req      Request handle.
 * @param status   HTTP status line.
 * @param message  Error message.
 * @return ESP_OK on success.
 */
static esp_err_t
http_server_send_error (httpd_req_t *req, const char *status, const char *message)
{
    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (false, message, body, sizeof (body));
    httpd_resp_set_status (req, status);
    httpd_resp_set_type (req, "application/json");
    return httpd_resp_send (req, body, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief Send a formatted JSON string with 200 OK.
 *
 * @param req   Request handle.
 * @param body  JSON body.
 * @return ESP_OK on success.
 */
static esp_err_t
http_server_send_json (httpd_req_t *req, const char *body)
{
    httpd_resp_set_type (req, "application/json");
    return httpd_resp_send (req, body, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief Read the request body into a NUL-terminated buffer.
 *
 * @param req       Request handle.
 * @param buf       Destination buffer.
 * @param buf_size  Destination size.
 * @return ESP_OK on success, otherwise an error.
 */
static esp_err_t
http_server_read_body (httpd_req_t *req, char *buf, size_t buf_size)
{
    if (req->content_len == 0 || req->content_len >= buf_size)
        {
            return ESP_ERR_INVALID_SIZE;
        }
    int received = httpd_req_recv (req, buf, req->content_len);
    if (received <= 0)
        {
            return ESP_FAIL;
        }
    buf[received] = '\0';
    return ESP_OK;
}

/** @brief GET / handler serving the embedded web UI. */
static esp_err_t
http_server_root_get (httpd_req_t *req)
{
    httpd_resp_set_type (req, "text/html");
    return httpd_resp_send (req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

/** @brief GET /api/status handler. */
static esp_err_t
http_server_status_get (httpd_req_t *req)
{
    const api_status_t status = {
        .system = s_system_name,
        .state = http_server_led_state_name (led_rgb_get_state ()),
        .wifi = http_server_wifi_state_name (app_wifi_get_state ()),
        .ip = http_server_ip_string (),
        .uptime_s = (uint32_t)(esp_timer_get_time () / 1000000),
        .free_heap = (uint32_t)esp_get_free_heap_size (),
    };
    char body[HTTP_SERVER_JSON_MAX];
    if (api_json_format_status (&status, body, sizeof (body)) != ESP_OK)
        {
            return http_server_send_error (req, "500 Internal Server Error", "format failed");
        }
    return http_server_send_json (req, body);
}

/** @brief GET /api/time handler. */
static esp_err_t
http_server_time_get (httpd_req_t *req)
{
    char iso[32];
    esp_err_t err = time_sync_get (iso, sizeof (iso));
    const api_time_t time = {
        .synced = time_sync_is_synced (),
        .epoch = time_sync_now (),
        .iso8601 = err == ESP_OK ? iso : "",
    };
    char body[HTTP_SERVER_JSON_MAX];
    if (api_json_format_time (&time, body, sizeof (body)) != ESP_OK)
        {
            return http_server_send_error (req, "500 Internal Server Error", "format failed");
        }
    return http_server_send_json (req, body);
}

/** @brief GET /api/led handler. */
static esp_err_t
http_server_led_get (httpd_req_t *req)
{
    led_rgb_output_t output;
    led_rgb_get_output ((uint32_t)(esp_timer_get_time () / 1000), &output);

    const api_led_state_t led = {
        .state = http_server_led_state_name (output.state),
        .r = output.color.r,
        .g = output.color.g,
        .b = output.color.b,
        .blink_ms = output.blink_ms,
        .on = output.on,
    };
    char body[HTTP_SERVER_JSON_MAX];
    if (api_json_format_led (&led, body, sizeof (body)) != ESP_OK)
        {
            return http_server_send_error (req, "500 Internal Server Error", "format failed");
        }
    return http_server_send_json (req, body);
}

/** @brief POST /api/led handler. */
static esp_err_t
http_server_led_post (httpd_req_t *req)
{
    char request[HTTP_SERVER_BODY_MAX];
    if (http_server_read_body (req, request, sizeof (request)) != ESP_OK)
        {
            return http_server_send_error (req, "400 Bad Request", "invalid body");
        }

    api_led_command_t command;
    if (api_json_parse_led (request, &command) != ESP_OK)
        {
            return http_server_send_error (req, "400 Bad Request", "invalid json");
        }

    const led_rgb_color_t color = {
        .r = (uint8_t)command.r,
        .g = (uint8_t)command.g,
        .b = (uint8_t)command.b,
    };
    esp_err_t err = led_rgb_set_custom (color, command.blink_ms);
    if (err != ESP_OK)
        {
            return http_server_send_error (req, "409 Conflict", "not connected");
        }

    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (true, "custom set", body, sizeof (body));
    return http_server_send_json (req, body);
}

/** @brief POST /api/led/clear handler. */
static esp_err_t
http_server_led_clear_post (httpd_req_t *req)
{
    if (led_rgb_clear_custom () != ESP_OK)
        {
            return http_server_send_error (req, "409 Conflict", "not in custom state");
        }
    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (true, "custom cleared", body, sizeof (body));
    return http_server_send_json (req, body);
}

/** @brief POST /api/restart handler. */
static esp_err_t
http_server_restart_post (httpd_req_t *req)
{
    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (true, "restarting", body, sizeof (body));
    http_server_send_json (req, body);
    esp_restart ();
    return ESP_OK;
}

/** @brief POST /api/reset handler. */
static esp_err_t
http_server_reset_post (httpd_req_t *req)
{
    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (true, "resetting", body, sizeof (body));
    http_server_send_json (req, body);
    storage_erase_wifi ();
    esp_restart ();
    return ESP_OK;
}

esp_err_t
http_server_start (uint16_t port, const char *system_name)
{
    if (system_name != NULL)
        {
            strncpy (s_system_name, system_name, sizeof (s_system_name) - 1);
        }

    snprintf (s_index_html, sizeof (s_index_html),
              "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%s</title></head>"
              "<body><h1>%s</h1><p>ESP32-S3 LED controller</p>"
              "<p><a href=\"/api/status\">status</a> | "
              "<a href=\"/api/time\">time</a> | "
              "<a href=\"/api/led\">led</a></p></body></html>",
              s_system_name, s_system_name);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
    config.server_port = port;
    config.max_uri_handlers = 10;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR (httpd_start (&server, &config), TAG, "start HTTP server");

    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = http_server_root_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = http_server_status_get },
        { .uri = "/api/time", .method = HTTP_GET, .handler = http_server_time_get },
        { .uri = "/api/led", .method = HTTP_GET, .handler = http_server_led_get },
        { .uri = "/api/led", .method = HTTP_POST, .handler = http_server_led_post },
        { .uri = "/api/led/clear", .method = HTTP_POST, .handler = http_server_led_clear_post },
        { .uri = "/api/restart", .method = HTTP_POST, .handler = http_server_restart_post },
        { .uri = "/api/reset", .method = HTTP_POST, .handler = http_server_reset_post },
    };
    for (size_t i = 0; i < sizeof (routes) / sizeof (routes[0]); i++)
        {
            ESP_RETURN_ON_ERROR (httpd_register_uri_handler (server, &routes[i]), TAG,
                                 "register %s", routes[i].uri);
        }

    ESP_LOGI (TAG, "HTTP server listening on port %u", port);
    return ESP_OK;
}
