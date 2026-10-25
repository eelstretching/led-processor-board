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

# The build time as Unix seconds (UTC), for seeding an RTC that lost power. Set at configure time,
# so re-run cmake for a fresher seed.
string(TIMESTAMP RTC_SET_BUILD_EPOCH "%s" UTC)
target_compile_definitions(rtc_set PRIVATE BUILD_EPOCH=${RTC_SET_BUILD_EPOCH}LL)
