add_library(ds3231 ds3231.cpp)

target_include_directories(ds3231 PUBLIC include)
target_compile_options(ds3231 PRIVATE -Wall -Wextra)

target_link_libraries(ds3231
    PUBLIC
        hardware_i2c
        pico_util
)

# Pin overrides for rtc_pins.h, e.g. -DRTC_SDA_PIN=26. Empty means the board's default.
foreach(var RTC_I2C_INDEX RTC_SDA_PIN RTC_SCL_PIN RTC_INT_PIN)
    if(NOT "${${var}}" STREQUAL "")
        target_compile_definitions(ds3231 PUBLIC ${var}=${${var}})
    endif()
endforeach()
