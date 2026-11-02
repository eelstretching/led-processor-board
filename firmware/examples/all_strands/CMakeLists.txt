# Every output the same: GREEN_LEN pixels green then RED_LEN red, on GPIO 0-31 (J5-J8), for seeing
# a whole prop lit at once. Override with e.g. -DALL_STRANDS_BRIGHTNESS=32 -DALL_STRANDS_GREEN=190.
set(ALL_STRANDS_GREEN 190 CACHE STRING "Green pixels at the start of every strand")
set(ALL_STRANDS_RED 10 CACHE STRING "Red pixels after the green ones")
set(ALL_STRANDS_BRIGHTNESS 16 CACHE STRING "all_strands brightness out of 256")

add_executable(all_strands main.cpp)

pico_generate_pio_header(all_strands ${CMAKE_CURRENT_LIST_DIR}/ws2811_parallel.pio)

target_compile_definitions(all_strands PRIVATE
    GREEN_LEN=${ALL_STRANDS_GREEN}
    RED_LEN=${ALL_STRANDS_RED}
    BRIGHTNESS=${ALL_STRANDS_BRIGHTNESS}
)

target_link_libraries(all_strands
    PRIVATE
        pico_stdlib
        hardware_pio
        hardware_dma
)

pico_add_extra_outputs(all_strands)
