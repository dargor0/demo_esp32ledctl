/**
 * @file mdns_service.c
 * @brief mDNS glue: advertise "<system-name>.local" and the HTTP service.
 *
 * SEQUENCE
 * --------
 *   1. build the DNS-safe hostname and the service instance name from the
 *      system name (pure helpers);
 *   2. mdns_init();
 *   3. set the hostname and instance name;
 *   4. register the "_http._tcp" service on the HTTP port.
 *
 * Started once, after the network interface has a valid IP.
 */
#include "mdns_service.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "mdns.h"
#include "mdns_name.h"

static const char *TAG = "mdns_service";

esp_err_t
mdns_service_start (const char *system_name, uint16_t http_port)
{
    char hostname[MDNS_MAX_HOSTNAME_LEN];
    char instance[MDNS_MAX_HOSTNAME_LEN];

    ESP_RETURN_ON_ERROR (mdns_build_hostname (system_name, hostname, sizeof (hostname)), TAG,
                         "build hostname");
    ESP_RETURN_ON_ERROR (mdns_build_instance_name (system_name, instance, sizeof (instance)), TAG,
                         "build instance");

    ESP_RETURN_ON_ERROR (mdns_init (), TAG, "init mDNS");
    ESP_RETURN_ON_ERROR (mdns_hostname_set (hostname), TAG, "set hostname");
    ESP_RETURN_ON_ERROR (mdns_instance_name_set (instance), TAG, "set instance name");
    ESP_RETURN_ON_ERROR (mdns_service_add (instance, "_http", "_tcp", http_port, NULL, 0), TAG,
                         "add HTTP service");

    ESP_LOGI (TAG, "advertising %s (_http._tcp:%u)", hostname, http_port);
    return ESP_OK;
}
