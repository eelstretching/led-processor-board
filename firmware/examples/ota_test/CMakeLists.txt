# Over-the-air update test (see firmware/ota/README.md). Three builds of the same program, so a push
# visibly changes something:
#   ota_test_slow    D1 + GP35 blink every 1000 ms
#   ota_test_fast    D1 + GP35 blink every 150 ms
#   ota_test_nobuy   blinks every 50 ms and never commits: the bootrom rolls it back after 16.7 s
# All three are try-before-you-buy images and need the WiFi credentials (firmware/wifi_secrets.cmake).

if(WIFI_SSID STREQUAL "" OR WIFI_PASSWORD STREQUAL "")
    message(WARNING "WIFI_SSID/WIFI_PASSWORD are not set -- ota_test can't join WiFi without them "
                    "(see firmware/README.md)")
endif()

function(add_ota_test name label blink_ms skip_commit)
    add_executable(${name} main.cpp)
    target_compile_definitions(${name} PRIVATE
        WIFI_SSID="${WIFI_SSID}"
        WIFI_PASSWORD="${WIFI_PASSWORD}"
        OTA_LABEL="${label}"
        BLINK_MS=${blink_ms}
        OTA_SKIP_COMMIT=${skip_commit}
        # Mark the image try-before-you-buy: after a flash-update boot the bootrom runs it under a
        # 16.7 s watchdog until it calls ota::commit().
        PICO_CRT0_IMAGE_TYPE_TBYB=1
    )
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${name} PRIVATE
        pico_stdlib
        pico_unique_id
        pico_cyw43_arch_lwip_poll
        pico_lwip_mdns
        ota
    )
    # USB serial on (as everywhere), so picotool load -f -x and tio work.
    pico_enable_stdio_usb(${name} 1)
    # USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
    pico_enable_stdio_uart(${name} 0)
    pico_add_extra_outputs(${name})
endfunction()

add_ota_test(ota_test_slow slow 1000 0)
add_ota_test(ota_test_fast fast 150 0)
add_ota_test(ota_test_nobuy nobuy 50 1)
