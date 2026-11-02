//
// All 32 outputs (GPIO 0-31, J5-J8 on the processor board) show the same thing: the first 190
// pixels green, the next 10 red, so a whole prop can be lit to see how it looks and to spot a
// strand that's short, dead or wired to the wrong output. Strands shorter than 200 just stop early.
//
// Every strand gets the same data, so one PIO state machine drives all 32 pins together: each bit
// is three words (all high, the bit on every pin, all low), fed by DMA from a prebuilt frame and
// resent every 20 ms. Seed pixels take red, green, blue in that order, like strand_test.
//
// Doesn't use PicoLEDs: one fixed picture is all it needs.
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2811_parallel.pio.h"

#ifndef GREEN_LEN
#define GREEN_LEN 190
#endif
#ifndef RED_LEN
#define RED_LEN 10
#endif
//
// Out of 256, applied to full green and full red.
#ifndef BRIGHTNESS
#define BRIGHTNESS 16
#endif

#define FIRST_PIN 0
#define PIN_COUNT 32
#define PIXELS (GREEN_LEN + RED_LEN)
#define WORDS (PIXELS * 24 * 3)
#define FRAME_MS 20

static uint32_t frame[WORDS];

static void build_frame() {
    uint8_t level = 255 * BRIGHTNESS / 256;
    size_t w = 0;
    for (int i = 0; i < PIXELS; i++) {
        uint32_t rgb = i < GREEN_LEN ? (uint32_t) level << 8 : (uint32_t) level << 16;
        for (int bit = 23; bit >= 0; bit--) {
            frame[w++] = 0xffffffffu;
            frame[w++] = (rgb >> bit) & 1 ? 0xffffffffu : 0;
            frame[w++] = 0;
        }
    }
}

int main() {
    build_frame();

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
    dma_channel_configure(dma, &c, &pio->txf[sm], frame, WORDS, false);

    while (true) {
        dma_channel_set_read_addr(dma, frame, true);
        dma_channel_wait_for_finish_blocking(dma);
        // The frame takes about 6 ms to go out; the pins then sit low, which is the latch gap.
        sleep_ms(FRAME_MS);
    }
}
