// Where the DS3231 is wired, for each board this firmware builds for. Pick the board with
// PICO_BOARD at configure time; each of these can also be overridden on its own with
// -DRTC_I2C_INDEX=... -DRTC_SDA_PIN=... -DRTC_SCL_PIN=... (via CMake's RTC_* cache variables).
//
//   led_processor_board  I2C0, SDA GPIO32, SCL GPIO33, INT GPIO34 (U6, 10k pull-ups on board)
//   pico2_w              I2C1, SDA GP26 (pin 31), SCL GP27 (pin 32): the perfboard compute board
//                        with an off-the-shelf DS3231 module
#ifndef RTC_PINS_H
#define RTC_PINS_H

#include <assert.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"

#if defined(LED_PROCESSOR_BOARD)
#ifndef RTC_I2C_INDEX
#define RTC_I2C_INDEX LED_PROCESSOR_BOARD_RTC_I2C
#endif
#ifndef RTC_SDA_PIN
#define RTC_SDA_PIN LED_PROCESSOR_BOARD_RTC_SDA_PIN
#endif
#ifndef RTC_SCL_PIN
#define RTC_SCL_PIN LED_PROCESSOR_BOARD_RTC_SCL_PIN
#endif
#ifndef RTC_INT_PIN
#define RTC_INT_PIN LED_PROCESSOR_BOARD_RTC_INT_PIN
#endif
#else
#ifndef RTC_I2C_INDEX
#define RTC_I2C_INDEX 1
#endif
#ifndef RTC_SDA_PIN
#define RTC_SDA_PIN 26
#endif
#ifndef RTC_SCL_PIN
#define RTC_SCL_PIN 27
#endif
#endif

#define RTC_I2C I2C_INSTANCE(RTC_I2C_INDEX)

#ifndef RTC_BAUDRATE
#define RTC_BAUDRATE (100 * 1000)
#endif

// The RP2350's I2C pins are fixed by GPIO number: n mod 4 is 0 for SDA and 1 for SCL on
// I2C0, 2 for SDA and 3 for SCL on I2C1. Catch a pin on the wrong bus at compile time rather
// than as a silent "not responding" on the bench.
static_assert(RTC_SDA_PIN % 4 == 2 * RTC_I2C_INDEX, "RTC_SDA_PIN isn't an SDA pin of RTC_I2C");
static_assert(RTC_SCL_PIN % 4 == 2 * RTC_I2C_INDEX + 1, "RTC_SCL_PIN isn't an SCL pin of RTC_I2C");

// Sets up the RTC's I2C bus: the peripheral, the two pin functions, and the internal pull-ups
// (both the processor board and the usual DS3231 modules have their own; these are just margin).
// Call before Ds3231::init().
static inline void rtcBusInit() {
    i2c_init(RTC_I2C, RTC_BAUDRATE);
    gpio_set_function(RTC_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(RTC_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(RTC_SDA_PIN);
    gpio_pull_up(RTC_SCL_PIN);
}

#endif // RTC_PINS_H
