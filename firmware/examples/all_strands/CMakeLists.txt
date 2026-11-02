# Every output the same: STRAND_LEN pixels green with every TICK_EVERY-th one red, on GPIO 0-31
# (J5-J8), for seeing a whole prop lit at once. Override with e.g. -DALL_STRANDS_BRIGHTNESS=32.
set(ALL_STRANDS_LEN 200 CACHE STRING "Pixels lit on every strand")
set(ALL_STRANDS_TICK 10 CACHE STRING "Every Nth pixel is red (0 = no ticks)")
set(ALL_STRANDS_BRIGHTNESS 16 CACHE STRING "all_strands brightness out of 256")

add_executable(all_strands main.cpp)

pico_generate_pio_header(all_strands ${CMAKE_CURRENT_LIST_DIR}/ws2811_parallel.pio)

target_compile_definitions(all_strands PRIVATE
    STRAND_LEN=${ALL_STRANDS_LEN}
    TICK_EVERY=${ALL_STRANDS_TICK}
    BRIGHTNESS=${ALL_STRANDS_BRIGHTNESS}
)

target_link_libraries(all_strands
    PRIVATE
        pico_stdlib
        hardware_pio
        hardware_dma
)

pico_add_extra_outputs(all_strands)
