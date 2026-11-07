// RM2 bring-up stress test. Each boot does exactly one cold bring-up of the radio (cyw43_arch_init
// plus a cyw43_gpio_set() on D1, which is what actually powers the chip up and loads its firmware),
// prints the result, and reboots through the watchdog. The tally lives in the watchdog scratch
// registers, which survive a watchdog reboot, so every try starts from a genuinely cold radio.
//
// A bring-up that hangs is caught by the watchdog (8 s) and counted as a FAIL, because the boot
// is booked before the try starts.
//
// Output, one line per boot, then a summary that repeats every 10 s:
//   >>> BOOT  7/20  full speed (div 2)      ok (512 ms, error 0)
//   >>> DONE. One bring-up per boot:
//   >>>   full speed (div 2)       20 / 20 ok
//   >>> press r to run it again
//
// The ".uf2 built 2026-11-01 for board #1" this replaces was never committed; this is a rebuild
// with the same output.
#include <cstdio>

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#ifndef BOOTS_PER_SPEED
#define BOOTS_PER_SPEED 20
#endif

// cyw43_bus_pio_spi.c's setter. Declared here because the SDK doesn't put it in a public header.
extern "C" void cyw43_set_pio_clkdiv_int_frac8(uint32_t clock_div_int, uint8_t clock_div_frac8);

struct Speed {
    uint32_t div;
    const char* name;
};

static const Speed SPEEDS[] = {
    {2, "full speed (div 2)"},
#ifdef TEST_SLOW_SPEEDS
    {4, "half speed (div 4)"},
    {8, "quarter speed (div 8)"},
#endif
};
static const int NUM_SPEEDS = sizeof(SPEEDS) / sizeof(SPEEDS[0]);
static const int TOTAL_BOOTS = NUM_SPEEDS * BOOTS_PER_SPEED;

// Watchdog scratch 0-3 are ours (the SDK's watchdog_reboot() uses 4-7).
#define MAGIC 0x52535452u  // "RSTR"
#define S_MAGIC 0
#define S_BOOT 1  // boots already booked
#define S_OK 2    // ok count per speed, 8 bits each

static uint32_t okCount(int speed) {
    return (watchdog_hw->scratch[S_OK] >> (8 * speed)) & 0xff;
}

static void addOk(int speed) {
    watchdog_hw->scratch[S_OK] += 1u << (8 * speed);
}

static void waitForUsb() {
    // Give tio a moment to reattach after each reboot so the line isn't lost.
    absolute_time_t until = make_timeout_time_ms(2500);
    while (!stdio_usb_connected() && absolute_time_diff_us(get_absolute_time(), until) > 0) {
        sleep_ms(10);
    }
    sleep_ms(200);
}

static void restart() {
    watchdog_hw->scratch[S_MAGIC] = MAGIC;
    watchdog_hw->scratch[S_BOOT] = 0;
    watchdog_hw->scratch[S_OK] = 0;
}

static void summary() {
    printf("\n>>> DONE. One bring-up per boot:\n");
    for (int s = 0; s < NUM_SPEEDS; s++) {
        printf(">>>   %-24s %2lu / %2lu ok\n", SPEEDS[s].name, (unsigned long) okCount(s),
               (unsigned long) BOOTS_PER_SPEED);
    }
    printf(">>> press r to run it again\n");
}

int main() {
    stdio_init_all();
    if (watchdog_hw->scratch[S_MAGIC] != MAGIC) {
        restart();  // first boot after flashing or a power cycle
    }
    waitForUsb();

    uint32_t boot = watchdog_hw->scratch[S_BOOT];
    if (boot < (uint32_t) TOTAL_BOOTS) {
        int speed = boot / BOOTS_PER_SPEED;
        // Book the boot first, so a hang that the watchdog catches still counts (as a FAIL).
        watchdog_hw->scratch[S_BOOT] = boot + 1;
        watchdog_enable(8000, true);

        cyw43_set_pio_clkdiv_int_frac8(SPEEDS[speed].div, 0);
        absolute_time_t t0 = get_absolute_time();
        int err = cyw43_arch_init();
        if (err == 0) {
            err = cyw43_gpio_set(&cyw43_state, CYW43_WL_GPIO_LED_PIN, true);
        }
        int ms = (int) (absolute_time_diff_us(t0, get_absolute_time()) / 1000);
        if (err == 0) {
            addOk(speed);
        }
        printf(">>> BOOT %2lu/%d  %-22s %s (%d ms, error %d)\n",
               (unsigned long) (boot % BOOTS_PER_SPEED + 1), BOOTS_PER_SPEED, SPEEDS[speed].name,
               err == 0 ? "ok" : "FAIL", ms, err);
        sleep_ms(300);  // let USB drain
        watchdog_reboot(0, 0, 0);
        while (true) {
            tight_loop_contents();
        }
    }

    // All done: show the tally until asked to go again.
    while (true) {
        summary();
        absolute_time_t until = make_timeout_time_ms(10000);
        while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
            int c = getchar_timeout_us(100000);
            if (c == 'r' || c == 'R') {
                restart();
                watchdog_reboot(0, 0, 0);
                while (true) {
                    tight_loop_contents();
                }
            }
        }
    }
}
