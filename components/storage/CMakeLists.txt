idf_build_get_property(target IDF_TARGET)

set(srcs "storage.c")
set(priv_requires "")

if(NOT target STREQUAL "linux")
    list(APPEND srcs "storage_nvs.c")
    list(APPEND priv_requires "nvs_flash")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
)

if(NOT target STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE STORAGE_HAVE_NVS=1)
endif()

# Host unit tests collect gcov coverage for this component.
if(target STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
