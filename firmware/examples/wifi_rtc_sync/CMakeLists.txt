add_executable(wifi_rtc_sync main.cpp)

if(WIFI_SSID STREQUAL "" OR WIFI_PASSWORD STREQUAL "")
    message(WARNING "WIFI_SSID/WIFI_PASSWORD are not set -- pass them with "
                     "-DWIFI_SSID=... -DWIFI_PASSWORD=... to configure "
                     "wifi_rtc_sync for your network")
endif()

target_compile_definitions(wifi_rtc_sync PRIVATE
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
)

target_include_directories(wifi_rtc_sync PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(wifi_rtc_sync
    PRIVATE
        pico_stdlib
        pico_cyw43_arch_lwip_threadsafe_background
        ds3231
)

pico_enable_stdio_usb(wifi_rtc_sync 1)
pico_enable_stdio_uart(wifi_rtc_sync 0)

pico_add_extra_outputs(wifi_rtc_sync)
