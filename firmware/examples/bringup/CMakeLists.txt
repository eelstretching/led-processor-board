# First power-on test for the processor board: blinks what can blink and reports over USB serial.
# Toggles HEARTBEAT_PIN (J2's GP35 by default) at 1 Hz; override with e.g. -DBRINGUP_HEARTBEAT_PIN=38.
set(BRINGUP_HEARTBEAT_PIN 35 CACHE STRING "GPIO toggled at 1 Hz by bringup")

add_executable(bringup main.cpp)

target_compile_definitions(bringup PRIVATE HEARTBEAT_PIN=${BRINGUP_HEARTBEAT_PIN})

target_link_libraries(bringup
    PRIVATE
        pico_stdlib
        pico_multicore
        pico_unique_id
        hardware_adc
        hardware_clocks
        hardware_flash
        pico_cyw43_arch_none
        ds3231
)

pico_enable_stdio_usb(bringup 1)
# USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
pico_enable_stdio_uart(bringup 0)

pico_add_extra_outputs(bringup)
