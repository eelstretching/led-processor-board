# Output-board level-shifter check: GPIO 0-31 (all four shrouds, J5-J8) all go high for a second,
# then low for a second, forever. D1 (the RM2's LED) and J2's GP35 blink along. Override the period
# with e.g. -DPIN_TEST_HALF_PERIOD_MS=2000.
set(PIN_TEST_HALF_PERIOD_MS 1000 CACHE STRING "pin_test: ms high, then ms low")

add_executable(pin_test main.cpp)

target_compile_definitions(pin_test PRIVATE HALF_PERIOD_MS=${PIN_TEST_HALF_PERIOD_MS})

target_link_libraries(pin_test
    PRIVATE
        pico_stdlib
        pico_multicore
        pico_cyw43_arch_none
)

# USB serial on, so the board shows up as a tty and `picotool load -f -x` can reboot it into
# BOOTSEL without the buttons.
pico_enable_stdio_usb(pin_test 1)
# USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
pico_enable_stdio_uart(pin_test 0)

pico_add_extra_outputs(pin_test)
