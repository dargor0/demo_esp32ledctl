idf_build_get_property(target IDF_TARGET)

set(srcs "time_format.c")
set(priv_requires "")

if(NOT target STREQUAL "linux")
    list(APPEND srcs "time_sync.c")
    list(APPEND priv_requires "esp_netif" "esp_timer" "freertos")
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
