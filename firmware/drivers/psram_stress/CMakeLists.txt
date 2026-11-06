# PSRAM stress tests (U4, APS6404L on GPIO47). An INTERFACE library, so psram.c from the SDK is
# compiled once per firmware with PICO_AUTO_DETECT_PSRAM_SIZE on: the SDK then reads the chip's ID
# at boot and only maps the PSRAM if a chip answers, instead of trusting the board header's 8 MB and
# bus-faulting on a board with a bad U4.
add_library(psram_stress INTERFACE)

target_sources(psram_stress INTERFACE ${CMAKE_CURRENT_LIST_DIR}/psram_stress.cpp)
target_include_directories(psram_stress INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(psram_stress INTERFACE PICO_AUTO_DETECT_PSRAM_SIZE=1)

target_link_libraries(psram_stress
    INTERFACE
        hardware_psram
        hardware_dma
        hardware_xip_cache
        pico_sync
)
