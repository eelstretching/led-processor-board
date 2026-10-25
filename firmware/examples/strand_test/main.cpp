//
// Strand tester: lights the first 200 pixels on GP2 a steady, dim green, for checking pixels and
// wiring while cutting reels into strands. GP2 is data 1 on the bank board ribbon when it's
// driven from a Pico 2 W (see PicoLEDs BankTest); on the processor board it's J5 (OUT1) data 3.
//
// Seed pixels take their bytes in red, green, blue order (PicoLEDs' ColorOrder::ORGB). The frame
// is resent every 20 ms so a glitch on the data line or a strand plugged in hot shows up right
// away. Shorter strands just ignore the extra pixels.
//
// Doesn't use PicoLEDs: one strand, one color, a plain PIO state machine is all it needs.
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2811.pio.h"

#ifndef STRAND_PIN
#define STRAND_PIN 2
#endif
#ifndef STRAND_LEN
#define STRAND_LEN 200
#endif
//
// Out of 256, applied to full green.
#ifndef BRIGHTNESS
#define BRIGHTNESS 16
#endif

#define FRAME_MS 20

int main() {
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ws2811_program);
    ws2811_program_init(pio, sm, offset, STRAND_PIN, 800000);

    uint8_t green = 255 * BRIGHTNESS / 256;
    //
    // Red, green, blue in the top 24 bits of the word, for the 24-bit autopull.
    uint32_t pixel = (uint32_t) green << 16;

    while (true) {
        for (int i = 0; i < STRAND_LEN; i++) {
            pio_sm_put_blocking(pio, sm, pixel);
        }
        // Holding the line low for the rest of the frame is the latch (reset) gap.
        sleep_ms(FRAME_MS);
    }
}
