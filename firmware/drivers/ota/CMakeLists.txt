# Over-the-air updates: A/B slots + try-before-you-buy (see firmware/ota/README.md).
# An INTERFACE library, so ota.cpp compiles with the linking firmware's lwipopts.h and cyw43_arch
# flavour (ota_test uses pico_cyw43_arch_lwip_poll).
add_library(ota INTERFACE)
target_sources(ota INTERFACE ${CMAKE_CURRENT_LIST_DIR}/ota.cpp)
target_include_directories(ota INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(ota INTERFACE
    pico_bootrom
    pico_flash
    pico_sha256
    hardware_flash
)
