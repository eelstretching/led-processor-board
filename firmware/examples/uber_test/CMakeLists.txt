# Uber-test: radio + WiFi + NTP -> RTC on core 1; Eastern clock, LEDs on GPIO 0-31 played from
# frames stored in the PSRAM, and a background PSRAM stress test on core 0. WiFi credentials come
# from firmware/wifi_secrets.cmake if set, otherwise the board asks over USB serial and keeps them
# in flash. -DUBER_BRIGHTNESS=32 for brighter strands, -DUBER_LEDS=0 to leave GPIO 0-31 alone.
set(UBER_BRIGHTNESS 16 CACHE STRING "uber_test strand brightness out of 256")
set(UBER_LEDS 1 CACHE STRING "uber_test: 1 = drive strands on GPIO 0-31, 0 = don't")

add_executable(uber_test main.cpp)

pico_generate_pio_header(uber_test ${CMAKE_CURRENT_LIST_DIR}/../all_strands/ws2811_parallel.pio)

target_compile_definitions(uber_test PRIVATE
    UBER_BRIGHTNESS=${UBER_BRIGHTNESS}
    UBER_LEDS=${UBER_LEDS}
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
)

# lwIP settings shared with wifi_rtc_sync.
target_include_directories(uber_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../wifi_rtc_sync)

target_link_libraries(uber_test
    PRIVATE
        pico_stdlib
        pico_multicore
        pico_cyw43_arch_lwip_threadsafe_background
        hardware_adc
        hardware_clocks
        hardware_dma
        hardware_flash
        hardware_pio
        ds3231
        psram_stress
)

pico_enable_stdio_usb(uber_test 1)
# USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
pico_enable_stdio_uart(uber_test 0)

pico_add_extra_outputs(uber_test)
