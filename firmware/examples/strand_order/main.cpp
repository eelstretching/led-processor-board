//
// Strand order check for the MegaTree: lights the 32 outputs (GPIO 0-31, J5-J8 on the processor
// board) one strand at a time, stepping through them in order, so once the strands are up the pole
// you can walk around the tree and see that strand 1, 2, 3, ... really go round in order.
//
// Every lit strand tells you which output it is, from the ground:
//   - its colour says the output shroud (output board): OUT1/J5 red, OUT2/J6 green, OUT3/J7 blue,
//     OUT4/J8 yellow;
//   - a row of white dots at each end (every other pixel) counts its data line on that shroud, 1-8,
//     which is the terminal on the output board: 3 dots = terminal 3. Then a short dark gap, then
//     the rest of the strand in the shroud's colour. The dots are at both ends, so it doesn't matter
//     which end of the strand is pixel 0.
// The serial port prints the same thing in big letters every time the strand changes.
//
// It boots auto-advancing, STEP_S seconds per strand, 1 to 32, then one dark step so you can see
// where the lap starts, then round again. Single-key controls on the USB serial port (tio):
//   n / space  next strand (stops auto-advance)     p  previous strand (stops auto-advance)
//   <number> Enter  jump to strand 1-32 (0 = all off), stops auto-advance
//   a  auto-advance on/off       + / -  slower / faster (1-30 s per strand)
//   c  cumulative on/off: strands 1..N all lit, so the lit wedge grows round the tree
//   b  brightness 8 -> 16 -> 32 -> 8 (out of 256; boots at BRIGHTNESS, 16)
//   ?  help and status
//
// Driven like all_strands: one PIO state machine on all 32 pins, three words per bit (all high, the
// bit on each pin, all low), DMA from a prebuilt frame resent every 20 ms, so strands can be plugged
// in hot. Unlit outputs get black. Seed pixels take red, green, blue in that order.
//
// Doesn't use PicoLEDs: a few fixed pictures are all it needs.
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "ws2811_parallel.pio.h"

#ifndef STRAND_LEN
#define STRAND_LEN 200
#endif
//
// Out of 256. The b key cycles through BRIGHTNESS_LEVELS and never goes above the last one.
#ifndef BRIGHTNESS
#define BRIGHTNESS 16
#endif
//
// Seconds per strand when auto-advancing.
#ifndef STEP_S
#define STEP_S 3
#endif

#define FIRST_PIN 0
#define STRANDS 32
#define PER_SHROUD 8
#define WORDS_PER_PIXEL (24 * 3)
#define FRAME_MS 20
#define STATUS_MS 15000
#define MIN_STEP_S 1
#define MAX_STEP_S 30
//
// Dark pixels between the last dot and the shroud colour.
#define DOT_GAP 3

static const int BRIGHTNESS_LEVELS[] = {8, 16, 32};
#define N_LEVELS ((int) (sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0])))

static const char *SHROUD_COLOUR_NAME[] = {"RED", "GREEN", "BLUE", "YELLOW"};

static uint32_t frame[STRAND_LEN * WORDS_PER_PIXEL];

//
// The strand showing, 0-31, or STRANDS for "all off" (the lap gap when auto-advancing, or a typed 0).
static int cur = 0;
static bool autoAdvance = true;
static bool cumulative = false;
static int stepS = STEP_S;
static int brightness = BRIGHTNESS;

static char line[8];
static int len = 0;

//
// 0xRRGGBB for shroud 0-3 at the current brightness.
static uint32_t shroud_colour(int shroud) {
    uint32_t l = (uint32_t) (255 * brightness / 256);
    switch (shroud) {
    case 0:
        return l << 16;
    case 1:
        return l << 8;
    case 2:
        return l;
    default:
        return (l << 16) | (l << 8);
    }
}

//
// What pixel i of strand s shows when it's lit: dots counting its data line at both ends, a gap,
// then the shroud colour.
static uint32_t pixel_colour(int s, int i) {
    int dots = s % PER_SHROUD + 1;
    int fromEnd = i < STRAND_LEN / 2 ? i : STRAND_LEN - 1 - i;
    if (fromEnd < 2 * dots - 1) {
        uint32_t l = (uint32_t) (255 * brightness / 256);
        return fromEnd % 2 == 0 ? (l << 16) | (l << 8) | l : 0;
    }
    if (fromEnd < 2 * dots - 1 + DOT_GAP) {
        return 0;
    }
    return shroud_colour(s / PER_SHROUD);
}

static bool strand_lit(int s) {
    if (cur >= STRANDS) {
        return false;
    }
    return cumulative ? s <= cur : s == cur;
}

static void build_frame() {
    uint32_t colour[STRANDS];
    for (int i = 0; i < STRAND_LEN; i++) {
        for (int s = 0; s < STRANDS; s++) {
            colour[s] = strand_lit(s) ? pixel_colour(s, i) : 0;
        }
        uint32_t *w = &frame[i * WORDS_PER_PIXEL];
        for (int bit = 23; bit >= 0; bit--) {
            uint32_t data = 0;
            for (int s = 0; s < STRANDS; s++) {
                data |= ((colour[s] >> bit) & 1) << s;
            }
            *w++ = 0xffffffffu;
            *w++ = data;
            *w++ = 0;
        }
    }
}

static void print_settings() {
    printf("    auto %s (%d s/strand), %s, brightness %d/256   [? = help]\n",
           autoAdvance ? "ON" : "off", stepS, cumulative ? "CUMULATIVE 1..N" : "one at a time",
           brightness);
}

static void print_strand() {
    if (cur >= STRANDS) {
        printf("\n\n>>> ALL OFF\n");
    } else {
        int shroud = cur / PER_SHROUD;
        int data = cur % PER_SHROUD + 1;
        printf("\n\n>>> STRAND %d of %d\n", cur + 1, STRANDS);
        printf("    OUT%d (J%d) data %d = output board %d terminal %d, GP%d\n", shroud + 1,
               shroud + 5, data, shroud + 1, data, FIRST_PIN + cur);
        printf("    looks like: %s with %d white dot%s at each end\n", SHROUD_COLOUR_NAME[shroud],
               data, data == 1 ? "" : "s");
    }
    print_settings();
}

static void print_help() {
    printf("\n\nstrand_order: one MegaTree strand at a time, GP0-GP31, %d px each\n", STRAND_LEN);
    printf("  colour = shroud: OUT1/J5 red, OUT2/J6 green, OUT3/J7 blue, OUT4/J8 yellow\n");
    printf("  white dots at each end = data line / terminal 1-8 on that shroud\n");
    printf("  n / space  next          p  previous      (both stop auto)\n");
    printf("  <num> Enter  jump to strand 1-%d, 0 = all off (stops auto)\n", STRANDS);
    printf("  a  auto on/off   + / -  slower / faster (%d-%d s)\n", MIN_STEP_S, MAX_STEP_S);
    printf("  c  cumulative on/off (strands 1..N all lit)\n");
    printf("  b  brightness %d -> %d -> %d\n", BRIGHTNESS_LEVELS[0], BRIGHTNESS_LEVELS[1],
           BRIGHTNESS_LEVELS[2]);
    printf("  ?  this help\n");
    print_strand();
}

static void step(int by) {
    // Positions 0..STRANDS, where STRANDS is all off; manual steps skip it.
    int span = autoAdvance ? STRANDS + 1 : STRANDS;
    int from = cur >= STRANDS ? (by > 0 ? -1 : 0) : cur;
    cur = ((from + by) % span + span) % span;
}

//
// Reads waiting serial input without blocking. Returns true if anything changed what's showing or
// the settings, so the caller rebuilds the frame and prints.
static bool poll_serial(absolute_time_t *nextStep) {
    bool changed = false;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c >= '0' && c <= '9') {
            if (len < (int) sizeof(line) - 1) {
                line[len++] = (char) c;
                putchar(c);
            }
            continue;
        }
        if ((c == '\b' || c == 127) && len > 0) {
            len--;
            printf("\b \b");
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (len == 0) {
                continue;
            }
            line[len] = 0;
            len = 0;
            int n = atoi(line);
            if (n > STRANDS) {
                printf("\n!!! no strand %d, there are %d", n, STRANDS);
                continue;
            }
            cur = n == 0 ? STRANDS : n - 1;
            autoAdvance = false;
            changed = true;
            continue;
        }
        len = 0;
        switch (c) {
        case 'n':
        case ' ':
            autoAdvance = false;
            step(1);
            changed = true;
            break;
        case 'p':
            autoAdvance = false;
            step(-1);
            changed = true;
            break;
        case 'a':
            autoAdvance = !autoAdvance;
            *nextStep = make_timeout_time_ms(stepS * 1000);
            changed = true;
            break;
        case '+':
        case '=':
            if (stepS < MAX_STEP_S) {
                stepS++;
            }
            changed = true;
            break;
        case '-':
        case '_':
            if (stepS > MIN_STEP_S) {
                stepS--;
            }
            changed = true;
            break;
        case 'c':
            cumulative = !cumulative;
            changed = true;
            break;
        case 'b': {
            int next = 0;
            for (int i = 0; i < N_LEVELS; i++) {
                if (BRIGHTNESS_LEVELS[i] > brightness) {
                    next = BRIGHTNESS_LEVELS[i];
                    break;
                }
            }
            brightness = next ? next : BRIGHTNESS_LEVELS[0];
            changed = true;
            break;
        }
        case '?':
        case 'h':
            print_help();
            break;
        default:
            break;
        }
    }
    return changed;
}

int main() {
    stdio_init_all();
    if (brightness > BRIGHTNESS_LEVELS[N_LEVELS - 1]) {
        brightness = BRIGHTNESS_LEVELS[N_LEVELS - 1];
    }
    build_frame();

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ws2811_parallel_program);
    ws2811_parallel_program_init(pio, sm, offset, FIRST_PIN, STRANDS, 800000);

    int dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma, &c, &pio->txf[sm], frame, 0, false);

    absolute_time_t nextStep = make_timeout_time_ms(stepS * 1000);
    absolute_time_t nextStatus = make_timeout_time_ms(2000);
    while (true) {
        dma_channel_set_trans_count(dma, STRAND_LEN * WORDS_PER_PIXEL, false);
        dma_channel_set_read_addr(dma, frame, true);
        dma_channel_wait_for_finish_blocking(dma);
        //
        // 200 px take 6 ms; the pins then sit low, which is the latch gap. The frame is only
        // rebuilt here, while the DMA is idle.
        absolute_time_t frameEnd = make_timeout_time_ms(FRAME_MS);
        bool changed = false;
        while (absolute_time_diff_us(get_absolute_time(), frameEnd) > 0) {
            changed |= poll_serial(&nextStep);
            sleep_ms(1);
        }
        if (autoAdvance && absolute_time_diff_us(get_absolute_time(), nextStep) <= 0) {
            step(1);
            nextStep = make_timeout_time_ms(stepS * 1000);
            changed = true;
        }
        if (changed) {
            build_frame();
            if (len == 0) {
                print_strand();
            }
            nextStatus = make_timeout_time_ms(STATUS_MS);
        } else if (len == 0 && absolute_time_diff_us(get_absolute_time(), nextStatus) <= 0) {
            // A reminder for a terminal opened mid-run; waits while a number is half typed.
            print_strand();
            nextStatus = make_timeout_time_ms(STATUS_MS);
        }
    }
}
