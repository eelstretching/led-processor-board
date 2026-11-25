# Lights the MegaTree's 32 outputs (GPIO 0-31, J5-J8) one strand at a time, so the strand order
# around the tree can be checked once it's up the pole. Serial controls on USB (tio), `?` for help.
# Override with e.g. -DSTRAND_ORDER_BRIGHTNESS=32.
set(STRAND_ORDER_LEN 200 CACHE STRING "Pixels on each strand")
set(STRAND_ORDER_BRIGHTNESS 16 CACHE STRING "strand_order boot brightness out of 256")
set(STRAND_ORDER_STEP_S 3 CACHE STRING "Seconds per strand when auto-advancing")

add_executable(strand_order main.cpp)

pico_generate_pio_header(strand_order ${CMAKE_CURRENT_LIST_DIR}/ws2811_parallel.pio)

target_compile_definitions(strand_order PRIVATE
    STRAND_LEN=${STRAND_ORDER_LEN}
    BRIGHTNESS=${STRAND_ORDER_BRIGHTNESS}
    STEP_S=${STRAND_ORDER_STEP_S}
)

target_link_libraries(strand_order
    PRIVATE
        pico_stdlib
        hardware_pio
        hardware_dma
)

# USB serial on, so the board shows up as a tty and `picotool load -f -x` can reboot it into
# BOOTSEL without the buttons.
pico_enable_stdio_usb(strand_order 1)
pico_enable_stdio_uart(strand_order 0)

pico_add_extra_outputs(strand_order)
