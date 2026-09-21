/**
 * @file http_server.c
 * @brief HTTP server: static web assets + path-based JSON REST API.
 *
 * ROUTES
 * ------
 *   GET  /            -> embedded index.html
 *   GET  /style.css   -> embedded CSS
 *   GET  /app.js      -> embedded JS
 *   GET  /api/status  -> system/WiFi/IP/uptime/heap + led_count + last_button_event
 *   GET  /api/time    -> synced/epoch/iso8601
 *   GET  /api/led            -> {count, leds:[...]}
 *   GET  /api/led/{id}       -> one LED (404 if invalid)
 *   PUT  /api/led/{id}       -> set one LED (404/400/409)
 *   PUT  /api/led            -> set all (single object) or listed set (array)
 *   DELETE /api/led/{id}     -> clear one (idempotent)
 *   DELETE /api/led          -> clear all / listed ids
 *   POST /api/reset          -> restart
 *   POST /api/wifi           -> erase creds -> provisioning (not in static mode)
 *
 * The LED id is extracted from the URI after "/api/led/". The wildcard matcher
 * (httpd_uri_match_wildcard) routes "/api/led/{id}" to the per-LED handlers. Any
 * LED component error maps to an HTTP status (INVALID_STATE -> 409, else 400).
 */
#include "http_server.h"

#include <stdlib.h>
#include <string.h>

#include "api_json.h"
#include "app_wifi.h"
#include "button.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "led_rgb.h"
#include "sdkconfig.h"
#include "storage.h"
#include "time_sync.h"

/** @brief System name buffer size. */
#define HTTP_SERVER_NAME_MAX 32
/** @brief Small JSON response buffer size. */
#define HTTP_SERVER_JSON_MAX 256
/** @brief Request body buffer size for single-LED writes. */
#define HTTP_SERVER_BODY_MAX 160
/** @brief Per-LED JSON budget for the collection response. */
#define HTTP_SERVER_LED_JSON_BUDGET 96

static const char *TAG = "http_server";

static char s_system_name[HTTP_SERVER_NAME_MAX];

/* Embedded web assets (see CMakeLists EMBED_FILES). */
extern const uint8_t index_html_start[] asm ("_binary_index_html_start");
extern const uint8_t index_html_end[] asm ("_binary_index_html_end");
extern const uint8_t style_css_start[] asm ("_binary_style_css_start");
extern const uint8_t style_css_end[] asm ("_binary_style_css_end");
extern const uint8_t app_js_start[] asm ("_binary_app_js_start");
extern const uint8_t app_js_end[] asm ("_binary_app_js_end");

/** @brief Map an LED state to its API name. */
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

/** @brief Map a WiFi state to its API name. */
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

/** @brief Get the station IPv4 address as a string. */
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

/** @brief Send a formatted JSON string with the given status line. */
static esp_err_t
http_server_send_json (httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status (req, status);
    httpd_resp_set_type (req, "application/json");
    return httpd_resp_send (req, body, HTTPD_RESP_USE_STRLEN);
}

/** @brief Send a JSON error response. */
static esp_err_t
http_server_send_error (httpd_req_t *req, const char *status, const char *message)
{
    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (false, message, body, sizeof (body));
    return http_server_send_json (req, status, body);
}

/** @brief Send a JSON success response. */
static esp_err_t
http_server_send_ok (httpd_req_t *req, const char *message)
{
    char body[HTTP_SERVER_JSON_MAX];
    api_json_format_ok (true, message, body, sizeof (body));
    return http_server_send_json (req, "200 OK", body);
}

/** @brief Read the request body into a NUL-terminated buffer. */
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

/** @brief Parse the LED id from a "/api/led/{id}" URI. */
static bool
http_server_led_id (const char *uri, uint16_t *id)
{
    static const char prefix[] = "/api/led/";
    if (strncmp (uri, prefix, sizeof (prefix) - 1) != 0)
        {
            return false;
        }
    const char *text = uri + sizeof (prefix) - 1;
    if (*text == '\0' || strchr (text, '/') != NULL)
        {
            return false;
        }
    char *end = NULL;
    long value = strtol (text, &end, 10);
    if (end == text || *end != '\0' || value < 0 || value > 65535)
        {
            return false;
        }
    *id = (uint16_t)value;
    return true;
}

/** @brief Fill one api_led_state_t from the LED state machine. */
static void
http_server_fill_led (uint16_t id, uint32_t now_ms, api_led_state_t *led)
{
    led_rgb_output_t out;
    led_rgb_get_output (id, now_ms, &out);
    led->id = id;
    led->state = http_server_led_state_name (out.state);
    led->r = out.color.r;
    led->g = out.color.g;
    led->b = out.color.b;
    led->blink_ms = out.blink_ms;
    led->on = out.on;
}

/** @brief Map a LED component error to an HTTP status line. */
static const char *
http_server_led_status (esp_err_t err)
{
    if (err == ESP_OK)
        {
            return "200 OK";
        }
    return err == ESP_ERR_INVALID_STATE ? "409 Conflict" : "400 Bad Request";
}

/* ------------------------------- static assets ------------------------------ */

static esp_err_t
http_server_root_get (httpd_req_t *req)
{
    httpd_resp_set_type (req, "text/html");
    return httpd_resp_send (req, (const char *)index_html_start, index_html_end - index_html_start);
}

static esp_err_t
http_server_css_get (httpd_req_t *req)
{
    httpd_resp_set_type (req, "text/css");
    return httpd_resp_send (req, (const char *)style_css_start, style_css_end - style_css_start);
}

static esp_err_t
http_server_js_get (httpd_req_t *req)
{
    httpd_resp_set_type (req, "application/javascript");
    return httpd_resp_send (req, (const char *)app_js_start, app_js_end - app_js_start);
}

/* ---------------------------------- status ---------------------------------- */

static esp_err_t
http_server_status_get (httpd_req_t *req)
{
    const api_status_t status = {
        .system = s_system_name,
        .state = http_server_led_state_name (led_rgb_get_state (0)),
        .wifi = http_server_wifi_state_name (app_wifi_get_state ()),
        .ip = http_server_ip_string (),
        .uptime_s = (uint32_t)(esp_timer_get_time () / 1000000),
        .free_heap = (uint32_t)esp_get_free_heap_size (),
        .led_count = (uint32_t)led_rgb_get_count (),
        .last_button_event = button_event_name (button_get_last_event ()),
    };
    char body[HTTP_SERVER_JSON_MAX];
    if (api_json_format_status (&status, body, sizeof (body)) != ESP_OK)
        {
            return http_server_send_error (req, "500 Internal Server Error", "format failed");
        }
    return http_server_send_json (req, "200 OK", body);
}

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
    return http_server_send_json (req, "200 OK", body);
}

/* ------------------------------------ LED ----------------------------------- */

static esp_err_t
http_server_led_get_all (httpd_req_t *req)
{
    size_t count = led_rgb_get_count ();
    api_led_state_t *states = calloc (count, sizeof (*states));
    size_t capacity = count * HTTP_SERVER_LED_JSON_BUDGET + 64;
    char *body = malloc (capacity);
    if (states == NULL || body == NULL)
        {
            free (states);
            free (body);
            return http_server_send_error (req, "500 Internal Server Error", "out of memory");
        }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time () / 1000);
    for (uint16_t id = 0; id < count; id++)
        {
            http_server_fill_led (id, now_ms, &states[id]);
        }

    esp_err_t err = api_json_format_led_list (states, count, body, capacity);
    free (states);
    if (err != ESP_OK)
        {
            free (body);
            return http_server_send_error (req, "500 Internal Server Error", "format failed");
        }
    esp_err_t send = http_server_send_json (req, "200 OK", body);
    free (body);
    return send;
}

static esp_err_t
http_server_led_get_one (httpd_req_t *req)
{
    uint16_t id;
    if (!http_server_led_id (req->uri, &id) || id >= led_rgb_get_count ())
        {
            return http_server_send_error (req, "404 Not Found", "unknown led");
        }

    api_led_state_t led;
    http_server_fill_led (id, (uint32_t)(esp_timer_get_time () / 1000), &led);
    char body[HTTP_SERVER_JSON_MAX];
    if (api_json_format_led (&led, body, sizeof (body)) != ESP_OK)
        {
            return http_server_send_error (req, "500 Internal Server Error", "format failed");
        }
    return http_server_send_json (req, "200 OK", body);
}

static esp_err_t
http_server_led_put_one (httpd_req_t *req)
{
    uint16_t id;
    if (!http_server_led_id (req->uri, &id) || id >= led_rgb_get_count ())
        {
            return http_server_send_error (req, "404 Not Found", "unknown led");
        }

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
    esp_err_t err = led_rgb_set_custom (id, color, command.blink_ms);
    if (err != ESP_OK)
        {
            return http_server_send_error (req, http_server_led_status (err), "cannot set");
        }
    return http_server_send_ok (req, "custom set");
}

static esp_err_t
http_server_led_delete_one (httpd_req_t *req)
{
    uint16_t id;
    if (!http_server_led_id (req->uri, &id) || id >= led_rgb_get_count ())
        {
            return http_server_send_error (req, "404 Not Found", "unknown led");
        }
    if (led_rgb_clear_custom (id) != ESP_OK)
        {
            return http_server_send_error (req, "400 Bad Request", "invalid id");
        }
    return http_server_send_ok (req, "custom cleared");
}

static esp_err_t
http_server_led_put_all (httpd_req_t *req)
{
    size_t capacity = led_rgb_get_count () * HTTP_SERVER_BODY_MAX;
    char *request = malloc (capacity);
    if (request == NULL)
        {
            return http_server_send_error (req, "500 Internal Server Error", "out of memory");
        }
    if (http_server_read_body (req, request, capacity) != ESP_OK)
        {
            free (request);
            return http_server_send_error (req, "400 Bad Request", "invalid body");
        }

    esp_err_t err;
    if (api_json_body_is_array (request))
        {
            api_led_entry_t *entries = calloc (API_JSON_MAX_LEDS, sizeof (*entries));
            led_rgb_cmd_t *cmds = calloc (API_JSON_MAX_LEDS, sizeof (*cmds));
            size_t count = 0;
            if (entries == NULL || cmds == NULL
                || api_json_parse_led_many (request, entries, API_JSON_MAX_LEDS, &count) != ESP_OK)
                {
                    free (entries);
                    free (cmds);
                    free (request);
                    return http_server_send_error (req, "400 Bad Request", "invalid json");
                }
            for (size_t i = 0; i < count; i++)
                {
                    cmds[i].id = entries[i].id;
                    cmds[i].color = (led_rgb_color_t){ (uint8_t)entries[i].r, (uint8_t)entries[i].g,
                                                       (uint8_t)entries[i].b };
                    cmds[i].blink_ms = entries[i].blink_ms;
                }
            free (entries);
            err = led_rgb_set_custom_many (cmds, count);
            free (cmds);
        }
    else
        {
            api_led_command_t command;
            if (api_json_parse_led (request, &command) != ESP_OK)
                {
                    free (request);
                    return http_server_send_error (req, "400 Bad Request", "invalid json");
                }
            const led_rgb_color_t color
                = { (uint8_t)command.r, (uint8_t)command.g, (uint8_t)command.b };
            err = led_rgb_set_custom_all (color, command.blink_ms);
        }
    free (request);

    if (err != ESP_OK)
        {
            return http_server_send_error (req, http_server_led_status (err), "cannot set");
        }
    return http_server_send_ok (req, "custom set");
}

static esp_err_t
http_server_led_delete_all (httpd_req_t *req)
{
    if (req->content_len == 0)
        {
            led_rgb_clear_custom_all ();
            return http_server_send_ok (req, "custom cleared");
        }

    char request[HTTP_SERVER_BODY_MAX];
    if (http_server_read_body (req, request, sizeof (request)) != ESP_OK)
        {
            return http_server_send_error (req, "400 Bad Request", "invalid body");
        }

    uint16_t *ids = calloc (API_JSON_MAX_LEDS, sizeof (*ids));
    size_t count = 0;
    if (ids == NULL)
        {
            return http_server_send_error (req, "500 Internal Server Error", "out of memory");
        }
    if (api_json_parse_ids (request, ids, API_JSON_MAX_LEDS, &count) != ESP_OK)
        {
            free (ids);
            return http_server_send_error (req, "400 Bad Request", "invalid json");
        }

    esp_err_t err
        = count == 0 ? led_rgb_clear_custom_all () : led_rgb_clear_custom_many (ids, count);
    free (ids);
    if (err != ESP_OK)
        {
            return http_server_send_error (req, "400 Bad Request", "invalid id");
        }
    return http_server_send_ok (req, "custom cleared");
}

/* ---------------------------------- system ---------------------------------- */

#ifndef CONFIG_APP_WIFI_STATIC_CREDS
static esp_err_t
http_server_wifi_reset_post (httpd_req_t *req)
{
    http_server_send_ok (req, "wifi reset; restarting");
    storage_erase_wifi ();
    esp_restart ();
    return ESP_OK;
}
#endif

static esp_err_t
http_server_reset_post (httpd_req_t *req)
{
    http_server_send_ok (req, "restarting");
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

    httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
    config.server_port = port;
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR (httpd_start (&server, &config), TAG, "start HTTP server");

    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = http_server_root_get },
        { .uri = "/style.css", .method = HTTP_GET, .handler = http_server_css_get },
        { .uri = "/app.js", .method = HTTP_GET, .handler = http_server_js_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = http_server_status_get },
        { .uri = "/api/time", .method = HTTP_GET, .handler = http_server_time_get },
        { .uri = "/api/led", .method = HTTP_GET, .handler = http_server_led_get_all },
        { .uri = "/api/led", .method = HTTP_PUT, .handler = http_server_led_put_all },
        { .uri = "/api/led", .method = HTTP_DELETE, .handler = http_server_led_delete_all },
        { .uri = "/api/led/{id}", .method = HTTP_GET, .handler = http_server_led_get_one },
        { .uri = "/api/led/{id}", .method = HTTP_PUT, .handler = http_server_led_put_one },
        { .uri = "/api/led/{id}", .method = HTTP_DELETE, .handler = http_server_led_delete_one },
#ifndef CONFIG_APP_WIFI_STATIC_CREDS
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = http_server_wifi_reset_post },
#endif
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
