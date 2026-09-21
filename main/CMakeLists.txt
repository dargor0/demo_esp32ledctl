set(app_requires
    led_rgb
    button
    storage
    app_wifi
    time_sync
    mdns_service
    http_server
    esp_timer
)

if(NOT CONFIG_APP_WIFI_STATIC_CREDS)
    list(APPEND app_requires provisioning)
endif()

idf_component_register(
    SRCS "esp32ledcontrol.c"
    INCLUDE_DIRS "."
    REQUIRES ${app_requires}
)
