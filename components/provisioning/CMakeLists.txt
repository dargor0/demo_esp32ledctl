idf_build_get_property(target IDF_TARGET)

set(srcs "provisioning_logic.c")
set(priv_requires "")

if(NOT target STREQUAL "linux")
    list(APPEND srcs "provisioning.c")
    # network_provisioning is provided by the project manifest (main/idf_component.yml).
    list(APPEND priv_requires "network_provisioning" "storage" "esp_wifi" "esp_event" "nvs_flash")
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
