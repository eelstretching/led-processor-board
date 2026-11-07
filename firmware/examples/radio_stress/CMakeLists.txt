# RM2 bring-up stress test: one cold radio bring-up per boot, watchdog reboot, tally kept in the
# watchdog scratch registers across reboots. -DRADIO_STRESS_BOOTS=n sets boots per speed (default
# 20); -DRADIO_STRESS_SLOW=1 also tries the gSPI bus at half and quarter speed.
set(RADIO_STRESS_BOOTS 20 CACHE STRING "radio_stress: bring-ups per bus speed")
option(RADIO_STRESS_SLOW "radio_stress: also test half and quarter gSPI speed" OFF)

add_executable(radio_stress main.cpp)

# CYW43_PIO_CLOCK_DIV_DYNAMIC makes the SDK compile in cyw43_set_pio_clkdiv_int_frac8().
target_compile_definitions(radio_stress PRIVATE BOOTS_PER_SPEED=${RADIO_STRESS_BOOTS} CYW43_PIO_CLOCK_DIV_DYNAMIC=1)
if(RADIO_STRESS_SLOW)
    target_compile_definitions(radio_stress PRIVATE TEST_SLOW_SPEEDS=1)
endif()

target_link_libraries(radio_stress
    PRIVATE
        pico_stdlib
        pico_cyw43_arch_none
        hardware_watchdog
)

# USB serial on, so the board shows up as a tty and `picotool load -f -x` can reboot it into
# BOOTSEL without the buttons.
pico_enable_stdio_usb(radio_stress 1)
# USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
pico_enable_stdio_uart(radio_stress 0)

pico_add_extra_outputs(radio_stress)
