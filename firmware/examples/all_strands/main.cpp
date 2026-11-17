//
// All 32 outputs (GPIO 0-31, J5-J8 on the processor board) show the same thing: N pixels green
// with every 10th one (10, 20, ...) red as a tick mark, so a whole prop can be lit to see how it
// looks and to spot a strand that's short, dead or wired to the wrong output. The last red tick on a
// strand says roughly where it ends; a full strand ends on a red pixel.
//
// N starts at STRAND_LEN (200). Type a number and press Enter on the USB serial port (tio) to light
// that many instead, up to MAX_PIXELS; 0 turns everything off. Pixels past N are sent black, so
// shrinking N really turns the tail off.
//
// Every strand gets the same data, so one PIO state machine drives all 32 pins together: each bit
// is three words (all high, the bit on every pin, all low), fed by DMA from a prebuilt frame and
// resent every 20 ms. Seed pixels take red, green, blue in that order, like strand_test.
//
// Doesn't use PicoLEDs: one fixed picture is all it needs.
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
// The most pixels a number typed on serial can light: a whole 1,000-pixel roll. The frame buffer
// is sized for this (1000 px x 24 bits x 3 words x 4 bytes = 288 KB of the RP2350's 520 KB).
#ifndef MAX_PIXELS
#define MAX_PIXELS 1000
#endif
//
// Every TICK_EVERY-th pixel is red; 0 turns the ticks off.
#ifndef TICK_EVERY
#define TICK_EVERY 10
#endif
//
// Out of 256, applied to full green and full red.
#ifndef BRIGHTNESS
#define BRIGHTNESS 16
#endif

#define FIRST_PIN 0
#define PIN_COUNT 32
#define WORDS_PER_PIXEL (24 * 3)
#define FRAME_MS 20
#define STATUS_MS 10000

static uint32_t frame[MAX_PIXELS * WORDS_PER_PIXEL];
//
// How many pixels are lit, and how many pixels each frame sends: the most ever lit, so pixels
// that were lit once and aren't now keep getting black.
static int lit = 0;
static int sendPixels = 0;
//
// Digits typed so far, waiting for Enter.
static char line[8];
static int len = 0;

static void set_pixel(int i, uint32_t rgb) {
    uint32_t *w = &frame[i * WORDS_PER_PIXEL];
    for (int bit = 23; bit >= 0; bit--) {
        *w++ = 0xffffffffu;
        *w++ = (rgb >> bit) & 1 ? 0xffffffffu : 0;
        *w++ = 0;
    }
}

static void build_frame(int n) {
    uint8_t level = 255 * BRIGHTNESS / 256;
    if (n > sendPixels) {
        sendPixels = n;
    }
    for (int i = 0; i < sendPixels; i++) {
        bool tick = TICK_EVERY > 0 && (i + 1) % TICK_EVERY == 0;
        uint32_t rgb = i >= n ? 0 : tick ? (uint32_t) level << 16 : (uint32_t) level << 8;
        set_pixel(i, rgb);
    }
    lit = n;
}

static void print_status() {
    printf("\n>>> LIT %d px on GP0-GP31 (tick every %d, brightness %d)\n", lit, TICK_EVERY,
           BRIGHTNESS);
    printf("type a number 0-%d + Enter: ", MAX_PIXELS);
}

//
// Reads whatever serial input is waiting, without blocking. Digits collect into a number, echoed
// so it shows in tio; Enter (CR or LF) relights with it; backspace deletes; anything else is
// ignored. Returns the new count when one was entered, otherwise -1.
static int poll_serial() {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c >= '0' && c <= '9') {
            if (len < (int) sizeof(line) - 1) {
                line[len++] = (char) c;
                putchar(c);
            }
        } else if ((c == '\b' || c == 127) && len > 0) {
            len--;
            printf("\b \b");
        } else if (c == '\r' || c == '\n') {
            if (len == 0) {
                continue;
            }
            line[len] = 0;
            len = 0;
            int n = atoi(line);
            if (n > MAX_PIXELS) {
                printf("\n!!! %d is more than %d, lighting %d", n, MAX_PIXELS, MAX_PIXELS);
                n = MAX_PIXELS;
            }
            return n;
        }
    }
    return -1;
}

int main() {
    stdio_init_all();
    build_frame(STRAND_LEN > MAX_PIXELS ? MAX_PIXELS : STRAND_LEN);

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ws2811_parallel_program);
    ws2811_parallel_program_init(pio, sm, offset, FIRST_PIN, PIN_COUNT, 800000);

    int dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma, &c, &pio->txf[sm], frame, 0, false);

    absolute_time_t nextStatus = get_absolute_time();
    while (true) {
        if (sendPixels > 0) {
            dma_channel_set_trans_count(dma, sendPixels * WORDS_PER_PIXEL, false);
            dma_channel_set_read_addr(dma, frame, true);
            dma_channel_wait_for_finish_blocking(dma);
        }
        //
        // A frame takes 1.25 us per bit (6 ms for 200 px, 30 ms for 1000); the pins then sit low,
        // which is the latch gap. The frame is only rebuilt here, while the DMA is idle.
        absolute_time_t frameEnd = make_timeout_time_ms(FRAME_MS);
        while (absolute_time_diff_us(get_absolute_time(), frameEnd) > 0) {
            int n = poll_serial();
            if (n >= 0) {
                build_frame(n);
                print_status();
                nextStatus = make_timeout_time_ms(STATUS_MS);
            }
            sleep_ms(1);
        }
        // The reminder waits while a number is half typed, so it doesn't break up the echo.
        if (len == 0 && absolute_time_diff_us(get_absolute_time(), nextStatus) <= 0) {
            print_status();
            nextStatus = make_timeout_time_ms(STATUS_MS);
        }
    }
}
