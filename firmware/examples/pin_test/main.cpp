// Pin test for output boards: GPIO 0-31 (every data line on J5-J8) go high together for a second,
// then low for a second, forever. Put a meter (or scope) on an output board's terminals: each one
// should swing between 0 V and the shifter's 5 V side in step. One that sits at 0 V or never drops
// is a bad level-shifter channel, 47R, or joint.
//
// D1 (the RM2's LED, on the radio's WL_GPIO0) is lit while the pins are high, so you can see it's
// running and which half of the cycle you're in. GP35 on J2 does the same, for boards without an
// RM2 yet. The radio is brought up on core 1 with retries, like bringup, so a missing or slow RM2
// never holds up the pins.
//
// USB serial prints the state each change, big enough to read across the bench:
//   >>> GP0-31 HIGH  (cycle 12)
#include <cstdio>

#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#ifndef HALF_PERIOD_MS
#define HALF_PERIOD_MS 1000
#endif

#define PIN_MASK 0xffffffffull  // GPIO 0-31
#define HEARTBEAT_PIN 35        // J2 spare header

#define RADIO_RETRY_MS 500
#define RADIO_MAX_TRIES 60

static volatile bool pinsHigh = false;

// Radio on core 1. Same bring-up dance as bringup: cyw43_gpio_set() returns the chip's bring-up
// result, and a failed try has to release the gSPI PIO by hand before the next.
static void radioMain() {
    int err = cyw43_arch_init();
    int tries = 0;
    while (err == 0) {
        tries++;
        err = cyw43_gpio_set(&cyw43_state, CYW43_WL_GPIO_LED_PIN, false);
        if (err == 0 || tries >= RADIO_MAX_TRIES) {
            break;
        }
        cyw43_thread_enter();
        cyw43_ll_deinit(&cyw43_state.cyw43_ll);
        cyw43_thread_exit();
        sleep_ms(RADIO_RETRY_MS);
    }
    if (err != 0) {
        printf("  radio: no answer after %d tries (cyw43 error %d), D1 stays dark; GP35 still blinks\n",
               tries, err);
        return;
    }
    printf("  radio: up on try %d, D1 follows the pins\n", tries);
    bool led = false;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
    while (true) {
        bool on = pinsHigh;
        if (on != led) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
            led = on;
        }
        sleep_ms(10);
    }
}

int main() {
    stdio_init_all();

    gpio_init_mask(PIN_MASK);
    gpio_set_dir_out_masked64(PIN_MASK);
    gpio_put_masked64(PIN_MASK, 0);
    gpio_init(HEARTBEAT_PIN);
    gpio_set_dir(HEARTBEAT_PIN, GPIO_OUT);

    multicore_launch_core1(radioMain);

    uint32_t cycle = 0;
    while (true) {
        bool high = !pinsHigh;
        if (high) {
            cycle++;
        }
        gpio_put_masked64(PIN_MASK, high ? PIN_MASK : 0);
        gpio_put(HEARTBEAT_PIN, high);
        pinsHigh = high;
        printf(">>> GP0-31 %s  (cycle %lu)\n", high ? "HIGH" : "low ", (unsigned long) cycle);
        sleep_ms(HALF_PERIOD_MS);
    }
}
