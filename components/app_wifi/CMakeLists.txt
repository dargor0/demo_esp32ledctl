idf_build_get_property(target IDF_TARGET)

set(srcs "wifi_logic.c")
set(priv_requires "")

if(NOT target STREQUAL "linux")
    list(APPEND srcs "app_wifi.c")
    list(APPEND priv_requires "esp_wifi" "esp_netif" "esp_event" "esp_timer")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
)

# Host unit tests collect gcov coverage for this component.
if(target STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
