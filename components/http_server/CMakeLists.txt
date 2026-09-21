idf_build_get_property(target IDF_TARGET)

set(srcs "api_json.c")
set(priv_requires "json")

if(NOT target STREQUAL "linux")
    list(APPEND srcs "http_server.c")
    list(APPEND priv_requires
        "esp_http_server"
        "led_rgb"
        "time_sync"
        "app_wifi"
        "storage"
        "button"
        "esp_netif"
        "esp_timer"
        "esp_system"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
    EMBED_FILES
        "www/index.html"
        "www/style.css"
        "www/app.js"
)

# Host unit tests collect gcov coverage for this component.
if(target STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
