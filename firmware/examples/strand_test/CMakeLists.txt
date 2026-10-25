# Strand tester: 200 pixels steady green at brightness 16/256 on GP2. Override with e.g.
# -DSTRAND_PIN=0 -DSTRAND_LEN=100 -DSTRAND_BRIGHTNESS=32.
set(STRAND_PIN 2 CACHE STRING "GPIO driving the strand for strand_test")
set(STRAND_LEN 200 CACHE STRING "Pixels lit by strand_test")
set(STRAND_BRIGHTNESS 16 CACHE STRING "strand_test brightness out of 256")

add_executable(strand_test main.cpp)

pico_generate_pio_header(strand_test ${CMAKE_CURRENT_LIST_DIR}/ws2811.pio)

target_compile_definitions(strand_test PRIVATE
    STRAND_PIN=${STRAND_PIN}
    STRAND_LEN=${STRAND_LEN}
    BRIGHTNESS=${STRAND_BRIGHTNESS}
)

target_link_libraries(strand_test
    PRIVATE
        pico_stdlib
        hardware_pio
)

pico_add_extra_outputs(strand_test)
