// Bring-up test for a freshly assembled processor board. Works with or without the RM2 soldered on.
//
// - GP35 (J2 spare header) toggles at 1 Hz, for a meter, scope, or an LED + resistor to GND. It's
//   the only blinkable thing before the RM2 goes on: D5 is the +5V power LED and D1 hangs off the
//   radio's WL_GPIO0.
// - Core 1 brings up the radio. With an RM2 fitted, D1 blinks in step with GP35; without one the
//   init fails (or never finishes) and the report says so, while core 0 carries on regardless.
// - Once a second over USB serial: uptime, die temperature, the DS3231's time and temperature,
//   and the radio's state. Every 10 s (so it's there whenever you connect) the full report: chip
//   version and package, system clock (measured, not just configured), flash ID and size, and a
//   scan of the RTC's I2C bus if the DS3231 doesn't answer.
//
// No GPIO 0-31 are touched, so it's safe with output boards and strands plugged in.
#include <cstdio>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/structs/sysinfo.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "ds3231.hpp"
#include "rtc_pins.h"

#ifndef HEARTBEAT_PIN
#define HEARTBEAT_PIN 35
#endif

enum class Radio { STARTING, UP, FAILED };
static volatile Radio radio = Radio::STARTING;
static volatile int radioError = 0;
static volatile int radioTries = 0;
static volatile uint32_t radioUpMs = 0;

// How hard to try before calling the radio missing. On the first v1.0 board the RM2 didn't answer
// when asked right after boot but did 8 s later, so keep asking for a while.
#define RADIO_RETRY_MS 500
#define RADIO_MAX_TRIES 60

// Read before core 1 starts: flash commands stop XIP, so nothing else may run from flash then.
static uint32_t flashJedecId = 0;

static void readFlashId() {
    uint8_t tx[4] = {0x9f, 0, 0, 0};
    uint8_t rx[4] = {};
    flash_do_cmd(tx, rx, sizeof(tx));
    flashJedecId = (rx[1] << 16) | (rx[2] << 8) | rx[3];
}

// Radio on core 1, so a missing or dead RM2 can't stall the rest of the test.
static void radioMain() {
    // cyw43_arch_init() only starts the driver; the chip itself is powered up and its firmware loaded
    // on first use. So drive D1 with cyw43_gpio_set(), which returns that bring-up's result
    // (cyw43_arch_gpio_put() throws it away), and retry until it answers or we give up.
    int err = cyw43_arch_init();
    while (err == 0) {
        radioTries = radioTries + 1;
        err = cyw43_gpio_set(&cyw43_state, CYW43_WL_GPIO_LED_PIN, false);
        if (err == 0) {
            break;
        }
        printf("  radio: try %d at %.1f s failed (cyw43 error %d)\n", radioTries,
               to_ms_since_boot(get_absolute_time()) / 1000.0f, err);
        if (radioTries >= RADIO_MAX_TRIES) {
            break;
        }
        // A failed bring-up leaves the gSPI bus's PIO state machine and program claimed, and
        // cyw43_deinit() skips its cleanup when the chip never came up, so each retry would claim
        // another one until the PIOs run out. Release the bus by hand before trying again.
        cyw43_thread_enter();
        cyw43_ll_deinit(&cyw43_state.cyw43_ll);
        cyw43_thread_exit();
        sleep_ms(RADIO_RETRY_MS);
    }
    if (err != 0) {
        radioError = err;
        radio = Radio::FAILED;
        return;
    }
    radioUpMs = to_ms_since_boot(get_absolute_time());
    radio = Radio::UP;
    printf("  radio: up on try %d, %.1f s after boot\n", radioTries, radioUpMs / 1000.0f);
    bool led = false;
    while (true) {
        // Follow core 0's heartbeat so D1 and GP35 blink together.
        bool on = gpio_get_out_level(HEARTBEAT_PIN);
        if (on != led) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
            led = on;
        }
        sleep_ms(10);
    }
}

static float dieTemperatureC() {
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    float volts = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (volts - 0.706f) / 0.001721f;
}

static void scanRtcBus() {
    printf("  I2C%d scan (SDA GP%d, SCL GP%d):", RTC_I2C_INDEX, RTC_SDA_PIN, RTC_SCL_PIN);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        if (i2c_read_timeout_us(RTC_I2C, addr, &dummy, 1, false, 2000) >= 0) {
            printf(" 0x%02x", addr);
            found++;
        }
    }
    printf(found ? "\n" : " nothing (U6 not powered or not soldered, or SDA/SCL stuck?)\n");
}

static const char *radioText() {
    static char text[64];
    switch (radio) {
    case Radio::STARTING:
        snprintf(text, sizeof(text), "starting (try %d)", radioTries);
        break;
    case Radio::UP:
        snprintf(text, sizeof(text), "up (try %d, %.1f s), D1 blinking", radioTries,
                 radioUpMs / 1000.0f);
        break;
    default:
        snprintf(text, sizeof(text), "no RM2 after %d tries", radioTries);
        break;
    }
    return text;
}

static void printReport(const Ds3231 &rtc) {
    char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id, sizeof(id));
    // PACKAGE_SEL reads 1 for the QFN-60 RP2350A, 0 for the QFN-80 RP2350B.
    bool isB = (sysinfo_hw->package_sel & 1) == 0;

    printf("\n==== LED processor board bring-up ====\n");
    printf("Chip:   RP2350%s, chip version %u, board ID %s\n", isB ? "B" : "A (expected B!)",
           rp2350_chip_version(), id);
    printf("Clocks: clk_sys %.1f MHz measured (%.1f configured), clk_usb %.1f MHz, XOSC %.1f MHz\n",
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000.0f, clock_get_hz(clk_sys) / 1e6f,
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB) / 1000.0f,
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC) / 1000.0f);
    uint8_t capacityCode = flashJedecId & 0xff;
    printf("Flash:  JEDEC ID %06lx", static_cast<unsigned long>(flashJedecId));
    if (capacityCode >= 0x10 && capacityCode <= 0x20) {
        printf(" (%lu MB)", static_cast<unsigned long>((1ul << capacityCode) >> 20));
    }
    printf("\n");
    printf("Rails:  no rail is wired to an ADC pin; if you can read this, 3V3 and 1V1 are up\n");
    printf("Blink:  GP%d on J2 at 1 Hz; D1 too once the RM2 is fitted (radio: %s", HEARTBEAT_PIN,
           radioText());
    if (radio == Radio::FAILED) {
        printf(", cyw43 error %d", radioError);
    }
    printf(")\n");
    if (rtc.init()) {
        printf("RTC:    DS3231 answers on I2C%d (SDA GP%d, SCL GP%d)%s\n", RTC_I2C_INDEX,
               RTC_SDA_PIN, RTC_SCL_PIN,
               rtc.lostPower() ? ", lost power since last set (expected on a new board: set it "
                                 "with rtc_set)"
                               : "");
    } else {
        printf("RTC:    DS3231 NOT answering\n");
        scanRtcBus();
    }
    printf("======================================\n");
}

static void printStatus(const Ds3231 &rtc, uint32_t seconds) {
    printf("[%5lus] die %.1f C | ", static_cast<unsigned long>(seconds), dieTemperatureC());
    std::optional<datetime_t> now;
    if (rtc.init()) {
        now = rtc.getDatetime();
    }
    if (now) {
        printf("RTC %04d-%02d-%02d %02d:%02d:%02d UTC", now->year, now->month, now->day, now->hour,
               now->min, now->sec);
        if (auto temp = rtc.getTemperatureC()) {
            printf(" %.2f C", *temp);
        }
    } else {
        printf("RTC not answering");
    }
    printf(" | radio %s\n", radioText());
}

int main() {
    readFlashId();
    stdio_init_all();

    gpio_init(HEARTBEAT_PIN);
    gpio_set_dir(HEARTBEAT_PIN, GPIO_OUT);

    adc_init();
    adc_set_temp_sensor_enabled(true);

    rtcBusInit();
    Ds3231 rtc(RTC_I2C);

    multicore_launch_core1(radioMain);

    absolute_time_t next = get_absolute_time();
    for (uint32_t halfSeconds = 0;; halfSeconds++) {
        gpio_put(HEARTBEAT_PIN, halfSeconds & 1);
        if (halfSeconds % 2 == 0) {
            uint32_t seconds = halfSeconds / 2;
            if (seconds % 10 == 0) {
                printReport(rtc);
            }
            printStatus(rtc, seconds);
        }
        next = delayed_by_ms(next, 500);
        sleep_until(next);
    }
}
