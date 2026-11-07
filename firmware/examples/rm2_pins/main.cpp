// RM2 joint check for the processor board. Doesn't start the cyw43 driver: it drives the four
// RP2350 pins that go to the RM2 (GP43-46) by hand, so you can find an open or bridged joint on
// the module with a meter.
//
// 1. Pull test. WL_ON (GP43) is held low so the module is off, then each of CS, DATA and CLK is
//    read with the internal pull-up and then the pull-down. A line that follows both is floating,
//    as it should be with the module off. One stuck low or high is shorted (or bridged to a
//    neighbouring GND or 3V3 pad on the RM2).
// 2. Probe. Each line in turn is held high for 20 s while the others are low, with a big label
//    on USB serial saying which RM2 castellation to put the red lead on. It should read 3.3 V
//    on the castellation itself (not just the board pad next to it). Any key gives another 20 s.
//
// Loops forever.
#include <cstdio>

#include "pico/stdlib.h"

#define HOLD_S 20
#define GAP_MS 3000

struct Rm2Line {
    unsigned gpio;
    const char* wiring;  // for the legend
    const char* pads;    // for the big probe label
};

static const Rm2Line LINES[] = {
    {43, "WL_ON+BT_ON  -> RM2 pins 12+13", "12 + 13"},
    {44, "CS           -> RM2 pin 9", "9"},
    {45, "DATA         -> RM2 pin 5 (DI), and pin 6 via R14 470R, pin 10 via R15 10k",
     "5  (6, 10 via resistors)"},
    {46, "CLK          -> RM2 pin 3", "3"},
};
static const int NUM_LINES = sizeof(LINES) / sizeof(LINES[0]);
static const unsigned WL_ON = 43;

static void allLow() {
    for (const Rm2Line& l : LINES) {
        gpio_init(l.gpio);
        gpio_disable_pulls(l.gpio);
        gpio_set_dir(l.gpio, GPIO_OUT);
        gpio_put(l.gpio, 0);
    }
}

static const char* verdict(int up, int down) {
    if (up && !down) {
        return "ok (floating, nothing fighting it)";
    }
    if (!up && !down) {
        return "STUCK LOW (short to GND, or something driving it low)";
    }
    if (up && down) {
        return "STUCK HIGH (short to 3V3, or something driving it high)";
    }
    return "odd";
}

static void pullTest() {
    printf("\nPull test (module off, GP43 held low):\n");
    allLow();
    sleep_ms(50);
    for (const Rm2Line& l : LINES) {
        if (l.gpio == WL_ON) {
            continue;
        }
        gpio_set_dir(l.gpio, GPIO_IN);
        gpio_pull_up(l.gpio);
        sleep_ms(5);
        int up = gpio_get(l.gpio);
        gpio_pull_down(l.gpio);
        sleep_ms(5);
        int down = gpio_get(l.gpio);
        gpio_disable_pulls(l.gpio);
        gpio_set_dir(l.gpio, GPIO_OUT);
        gpio_put(l.gpio, 0);
        printf("  GP%u %-62s pull-up reads %d, pull-down reads %d  %s\n", l.gpio, l.wiring, up, down,
               verdict(up, down));
    }
}

// True if a key arrived on USB serial; drains whatever was typed.
static bool keyPressed() {
    bool any = false;
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
        any = true;
    }
    return any;
}

static void probe() {
    printf("\nEach line is held high for %d s in turn; press any key to stay on a pin another %d s:\n",
           HOLD_S, HOLD_S);
    for (const Rm2Line& l : LINES) {
        printf("  GP%u HIGH (held): %s\n", l.gpio, l.wiring);
    }
    for (const Rm2Line& l : LINES) {
        allLow();
        gpio_put(l.gpio, 1);
        for (int left = HOLD_S; left > 0; left--) {
            printf(">>> RM2 PIN %-26s  (GP%u high, %2d s left)\n", l.pads, l.gpio, left);
            sleep_ms(1000);
            if (keyPressed()) {
                printf("    (key pressed: another %d s on this pin)\n", HOLD_S);
                left = HOLD_S + 1;
            }
        }
        allLow();
        printf("  all low for %d s\n", GAP_MS / 1000);
        sleep_ms(GAP_MS);
    }
}

int main() {
    stdio_init_all();
    allLow();
    // Give tio a moment to reconnect after a flash so the pull test isn't missed.
    sleep_ms(3000);
    while (true) {
        printf("\n==== rm2_pins: RM2 joint check ====\n");
        pullTest();
        probe();
    }
}
