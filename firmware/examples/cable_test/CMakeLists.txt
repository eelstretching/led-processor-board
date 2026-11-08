# IDC ribbon cable tester: plug one ribbon between any two of J5-J8 and it checks the wiring, sweeps
# every data line at 4.5 Mbit/s in both directions with crosstalk patterns, then soaks until the
# cable comes out. No options. Build it for led_processor_board.
add_executable(cable_test main.cpp)

target_link_libraries(cable_test
    PRIVATE
        pico_stdlib
        hardware_pio
        hardware_dma
)

# USB serial on, so the board shows up as a tty and `picotool load -f -x` can reboot it into
# BOOTSEL without the buttons.
pico_enable_stdio_usb(cable_test 1)
pico_enable_stdio_uart(cable_test 0)

pico_add_extra_outputs(cable_test)
