idf_component_register(
    SRCS
        "mimi.c"
        "buttons/multi_button.c"
        "buttons/button_driver.c"
        "imu/I2C_Driver.c"
        "imu/QMI8658.c"
        "imu/imu_manager.c"
        "bus/message_bus.c"
        "wifi/wifi_manager.c"
        "telegram/telegram_bot.c"
        "llm/llm_proxy.c"
        "agent/agent_loop.c"
        "agent/context_builder.c"
        "memory/memory_store.c"
        "memory/heap_utils.c"
        "memory/session_mgr.c"
        "gateway/ws_server.c"
        "cli/serial_cli.c"
        "cli/ble_cli.c"
        "proxy/http_proxy.c"
        "cron/cron_service.c"
        "heartbeat/heartbeat.c"
        "tools/tool_registry.c"
        "tools/tool_cron.c"
        "tools/tool_web_search.c"
        "tools/tool_get_time.c"
        "tools/tool_files.c"
        "tools/tool_sdcard.c"
        "skills/skill_loader.c"
        "hardware/echoear_config.c"
        "display/display_panel.cpp"
        "display/lvgl_v8_port.cpp"
        "ui/config_screen.c"
        "ui/touch_calibration_screen.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_http_client esp_http_server
        esp_https_ota esp_event json spiffs console vfs app_update esp-tls
        driver esp_timer bt esp32_display_panel lvgl fatfs sdmmc esp_driver_sdmmc
)

set(TARGET_COMPONENT "")
idf_build_get_property(build_components BUILD_COMPONENTS)
foreach(COMPONENT ${build_components})
    if(COMPONENT MATCHES "lvgl" OR COMPONENT MATCHES "lvgl__lvgl")
        set(TARGET_COMPONENT ${COMPONENT})
        break()
    endif()
endforeach()

if(NOT TARGET_COMPONENT STREQUAL "")
    idf_component_get_property(LVGL_LIB ${TARGET_COMPONENT} COMPONENT_LIB)
    target_include_directories(${LVGL_LIB} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
