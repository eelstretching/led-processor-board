//
// Strand tester: lights the first 200 pixels on GP2 a steady, dim green, for checking pixels and
// wiring while cutting reels into strands. GP2 is data 1 on the bank board ribbon when it's
// driven from a Pico 2 W (see PicoLEDs BankTest); on the processor board it's J5 (OUT1) data 3.
//
// Seed pixels take their bytes in red, green, blue order (PicoLEDs' ColorOrder::ORGB). The frame
// is resent every 20 ms so a glitch on the data line or a strand plugged in hot shows up right
// away. Shorter strands just ignore the extra pixels.
//
// A second pin (GP3 by default) is for counting a strand's pixels: it lights the first 190 green,
// then adds the last 10 one per second in red, holds all 200 for a few seconds, and starts over.
// Count the red pixels: ten means the strand has 200, fewer means it's short by the difference.
// GP3 is ribbon data 2 from a Pico 2 W; on the processor board it's J5 (OUT1) data 4.
//
// Doesn't use PicoLEDs: two strands and two colors, plain PIO state machines are all it needs.
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

#ifndef COUNT_PIN
#define COUNT_PIN 3
#endif
//
// The count pin lights COUNT_BASE pixels at once, then one more every COUNT_STEP_MS until
// STRAND_LEN, holds for COUNT_HOLD_MS and starts again.
#ifndef COUNT_BASE
#define COUNT_BASE 190
#endif
#ifndef COUNT_STEP_MS
#define COUNT_STEP_MS 1000
#endif
#ifndef COUNT_HOLD_MS
#define COUNT_HOLD_MS 5000
#endif

#define FRAME_MS 20

//
// How many pixels the count pin lights at time ms into the cycle.
static int countLit(uint32_t ms) {
    const uint32_t steps = STRAND_LEN - COUNT_BASE;
    const uint32_t cycle = steps * COUNT_STEP_MS + COUNT_HOLD_MS;
    uint32_t step = (ms % cycle) / COUNT_STEP_MS;
    return COUNT_BASE + (step < steps ? step : steps);
}

int main() {
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ws2811_program);
    ws2811_program_init(pio, sm, offset, STRAND_PIN, 800000);
    uint countSm = pio_claim_unused_sm(pio, true);
    ws2811_program_init(pio, countSm, offset, COUNT_PIN, 800000);

    uint8_t green = 255 * BRIGHTNESS / 256;
    //
    // Red, green, blue in the top 24 bits of the word, for the 24-bit autopull.
    uint32_t pixel = (uint32_t) green << 16;
    uint32_t counted = (uint32_t) green << 24;

    absolute_time_t start = get_absolute_time();
    while (true) {
        int lit = countLit(to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(start));
        //
        // Interleaved so both strands go out together; the TX FIFOs keep both state machines fed.
        for (int i = 0; i < STRAND_LEN; i++) {
            pio_sm_put_blocking(pio, sm, pixel);
            pio_sm_put_blocking(pio, countSm, i < COUNT_BASE ? pixel : i < lit ? counted : 0);
        }
        // Holding the line low for the rest of the frame is the latch (reset) gap.
        sleep_ms(FRAME_MS);
    }
}
