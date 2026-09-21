cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../..")

set(COMPONENTS "main")

# Host tests exercise a 4-LED chain.
idf_build_set_property(COMPILE_DEFINITIONS "CONFIG_APP_LED_COUNT=4" APPEND)

project(led_rgb_host_test)
