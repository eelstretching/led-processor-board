add_executable(rtc_set main.cpp)

target_link_libraries(rtc_set
    PRIVATE
        pico_stdlib
        ds3231
)

pico_enable_stdio_usb(rtc_set 1)
# USB only: on a Pico 2 W with nothing on GP1, a floating UART RX pin reads as junk bytes.
pico_enable_stdio_uart(rtc_set 0)

pico_add_extra_outputs(rtc_set)
