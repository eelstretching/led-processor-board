# RM2 joint check: pull test on the radio's gSPI lines with the module held off, then each line
# (GP43-46) held high for 20 s in turn so you can probe the RM2's castellations. No cyw43 driver.
add_executable(rm2_pins main.cpp)

target_link_libraries(rm2_pins
    PRIVATE
        pico_stdlib
)

# USB serial on, so the board shows up as a tty and `picotool load -f -x` can reboot it into
# BOOTSEL without the buttons.
pico_enable_stdio_usb(rm2_pins 1)
# USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
pico_enable_stdio_uart(rm2_pins 0)

pico_add_extra_outputs(rm2_pins)
