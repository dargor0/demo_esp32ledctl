idf_component_register(
    SRCS "esp32ledcontrol.c"
    INCLUDE_DIRS "."
    REQUIRES
        led_rgb
        button
        storage
        app_wifi
        time_sync
        mdns_service
        provisioning
        http_server
        esp_timer
)
