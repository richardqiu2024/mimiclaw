# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include_directories(${CMAKE_CURRENT_LIST_DIR}/main/conf)
project(mimiclaw)

set(DISPLAY_PANEL_COMPONENT "")
idf_build_get_property(build_components BUILD_COMPONENTS)
foreach(COMPONENT ${build_components})
    if(COMPONENT MATCHES "esp32_display_panel" OR COMPONENT MATCHES "espressif__esp32_display_panel")
        set(DISPLAY_PANEL_COMPONENT ${COMPONENT})
        break()
    endif()
endforeach()

if(NOT DISPLAY_PANEL_COMPONENT STREQUAL "")
    idf_component_get_property(DISPLAY_PANEL_LIB ${DISPLAY_PANEL_COMPONENT} COMPONENT_LIB)
    target_compile_definitions(
        ${DISPLAY_PANEL_LIB}
        PUBLIC
            ESP_PANEL_BOARD_CUSTOM_FILE_PATH=${CMAKE_CURRENT_LIST_DIR}/main/conf/esp_panel_board_custom_conf.h
    )
endif()

# Pre-flash a valid SPIFFS image so first boot does not need runtime formatting.
spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)
