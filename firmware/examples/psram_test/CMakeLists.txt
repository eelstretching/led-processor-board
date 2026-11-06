# PSRAM stress test (U4). Alternates passes between 150 MHz and PSRAM_TEST_FAST_KHZ; above 200 MHz
# it raises the core voltage to 1.20 V for the pass. e.g. -DPSRAM_TEST_FAST_KHZ=250000 to push harder.
set(PSRAM_TEST_FAST_KHZ 200000 CACHE STRING "psram_test: clk_sys for every other pass, in kHz")

add_executable(psram_test main.cpp)

target_compile_definitions(psram_test PRIVATE PSRAM_TEST_FAST_KHZ=${PSRAM_TEST_FAST_KHZ})

target_link_libraries(psram_test
    PRIVATE
        pico_stdlib
        pico_multicore
        hardware_clocks
        hardware_vreg
        psram_stress
)

pico_enable_stdio_usb(psram_test 1)
# USB only: the default UART's RX (GP37 on the board) floats on an empty J2 and reads as junk.
pico_enable_stdio_uart(psram_test 0)

pico_add_extra_outputs(psram_test)
