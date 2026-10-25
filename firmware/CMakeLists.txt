# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Builds for the LED processor board by default. For the perfboard compute board (a Pico 2 W with
# a DS3231 module on GP26/GP27), configure with -DPICO_BOARD=pico2_w. include/rtc_pins.h in the
# DS3231 driver picks the RTC's bus and pins from the board.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Optional per-pin overrides for the RTC, e.g. -DRTC_SDA_PIN=26 -DRTC_SCL_PIN=27 -DRTC_I2C_INDEX=1.
set(RTC_I2C_INDEX "" CACHE STRING "RTC I2C peripheral (0 or 1); empty = board default")
set(RTC_SDA_PIN "" CACHE STRING "RTC SDA GPIO; empty = board default")
set(RTC_SCL_PIN "" CACHE STRING "RTC SCL GPIO; empty = board default")
set(RTC_INT_PIN "" CACHE STRING "RTC INT/SQW GPIO; empty = board default")

# WiFi credentials for the wifi_rtc_sync example, kept out of git. Any one of:
#   - firmware/wifi_secrets.cmake (gitignored), containing
#       set(WIFI_SSID "myssid")
#       set(WIFI_PASSWORD "mypassword")
#   - WIFI_SSID / WIFI_PASSWORD environment variables
#   - cmake -DWIFI_SSID=... -DWIFI_PASSWORD=...
include(${CMAKE_CURRENT_LIST_DIR}/wifi_secrets.cmake OPTIONAL)
if(NOT DEFINED WIFI_SSID)
    set(WIFI_SSID "$ENV{WIFI_SSID}")
endif()
if(NOT DEFINED WIFI_PASSWORD)
    set(WIFI_PASSWORD "$ENV{WIFI_PASSWORD}")
endif()
set(WIFI_SSID "${WIFI_SSID}" CACHE STRING "WiFi SSID for wifi_rtc_sync")
set(WIFI_PASSWORD "${WIFI_PASSWORD}" CACHE STRING "WiFi password for wifi_rtc_sync")

include(pico_sdk_import.cmake)

project(led_processor_board_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The DS3231 driver uses the SDK's datetime_t, which is only built in on the RP2040 (whose own RTC
# it backs) unless asked for. Must come before pico_sdk_init().
add_compile_definitions(PICO_INCLUDE_RTC_DATETIME=1)

pico_sdk_init()

add_subdirectory(drivers/ds3231)

add_subdirectory(examples/rtc_set)
add_subdirectory(examples/wifi_rtc_sync)
