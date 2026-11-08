// IDC ribbon cable tester. Plug one ribbon between any two output shrouds on the processor board
// (J5-J8). The board works out which two shrouds you used, checks the wiring, then sends fast data
// down every data line in both directions and reads it back at the other end.
//
// 1. Wiring (DC). All 32 pins pulled up, then pulled down: a data line that won't follow the pulls
//    is shorted to GND or to 3.3 V / 5 V in the cable. Then each pin in turn is pulled low (weak
//    drive) and every other pin is read, which maps the cable: data n on one shroud should land on
//    data n on the other and nowhere else. Opens, crossed lines and line-to-line shorts show up
//    here. Lines that fail this are left out of the speed tests (they'd only fail them).
//
// 2. Speed (eye sweep). One PIO state machine drives the 8 data lines on one shroud and samples the
//    8 on the other, one bit every 33 PIO cycles (4.5 Mbit/s per line at 150 MHz, about 5.7x the
//    LEDs' 800 kHz). The sample is taken s cycles after the drive edge, and s is swept from 1 to 32
//    (6.7 ns to 213 ns), so a line's "settle" time is the earliest sample point from which it never
//    makes a mistake. A good line settles in a few ns. A bad IDC contact (a high-resistance or
//    intermittent joint) slows the edges, which is exactly what squashes the LEDs' 400 ns / 800 ns
//    pulses and gives weird colours. The input synchronisers are bypassed on the receiving pins so
//    the timing is real, not padded by two clock cycles.
//
//    Patterns, for each line in turn as the "victim":
//      - random data on all lines
//      - the victim alone toggling, the rest quiet (also checks the rest for crosstalk)
//      - the victim held 0 while the other 7 all toggle together (crosstalk onto a quiet line)
//      - the victim held 1 while the other 7 all toggle together
//      - the victim toggling opposite to the other 7 (the worst case for edge slowdown)
//    A quiet line that ever reads wrong is crosstalk; its "xtalk" time is how long after the
//    aggressors' edge it can still be wrong.
//
//    Grades per line: PASS if settle and crosstalk are both under 53 ns (8 cycles), WEAK under
//    147 ns (still inside what a WS2812-style pixel tolerates, but much worse than a good line),
//    FAIL beyond that or if it never comes good. Both directions are tested; the worse one counts.
//
// 3. Soak. After the report it keeps going until you unplug the cable: random and
//    toggling data both ways, sampled at the PASS point (53 ns), with a running error count per
//    line once a second. Wiggle and bend the cable at the connectors while it runs; a marginal crimp
//    shows up as errors on the line you're flexing. Pull the cable out and it goes back to waiting
//    for the next one, keeping a tally. Press r to rerun the full test on the cable that's in.
//
// It can't test the power and ground conductors (pins 1-11 odd = GND, 13 = 3.3 V, 15 = 5 V): both
// ends land on the same nets on this board, so they look connected whether the wire is good or
// not. Check those with a meter on continuity, pin to pin, with the cable out.
//
// Unplug the output boards first: only the ribbon under test should be on J5-J8.
#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "pico/stdlib.h"

#define SHROUDS 4
#define LANES 8
#define ALL_PINS 0xffffffffu

// Bit period in PIO cycles, and the sweep range for the sample point.
#define PERIOD 33
#define S_MIN 1
#define S_MAX 32
// Grade thresholds, in PIO cycles after the edge.
#define S_PASS 8
#define S_WEAK 22

#define TEST_WORDS 4096   // 16384 bit-times per line per sweep step
#define SOAK_WORDS 8192

static uint32_t txbuf[SOAK_WORDS];
static uint32_t rxbuf[SOAK_WORDS];

static PIO pio = pio0;
static uint sm;
static int dmaTx, dmaRx;
static float nsPerCycle;

// ---------------------------------------------------------------------------------------------
// Names

static int jNum(int shroud) { return 5 + shroud; }
static int outNum(int shroud) { return 1 + shroud; }
static int gpioOf(int shroud, int lane) { return shroud * LANES + lane; }

// ---------------------------------------------------------------------------------------------
// Wiring check (DC)

enum LaneFault { L_OK, L_GND, L_POWER, L_OPEN, L_CROSSED, L_SHORTED };

struct Wiring {
    bool present;
    int a, b;          // shroud indices, a < b
    uint32_t lowMask;  // pins that stay low with a pull-up: shorted to GND
    uint32_t highMask; // pins that stay high with a pull-down: shorted to 3.3 V / 5 V
    uint32_t conn[32]; // pins that follow pin g when it's pulled low
    LaneFault fault[LANES];
    int other[LANES];  // for CROSSED / SHORTED: which other data line (0-7)
    uint8_t goodLanes; // bitmask of lanes that passed the wiring check
};

static void allInputs(bool pullUp) {
    for (int g = 0; g < 32; g++) {
        gpio_set_dir(g, GPIO_IN);
        gpio_set_pulls(g, pullUp, !pullUp);
    }
}

static void resetPins() {
    gpio_init_mask(ALL_PINS);
    for (int g = 0; g < 32; g++) {
        gpio_set_input_enabled(g, true);
        gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_2MA);
    }
    pio->input_sync_bypass &= ~ALL_PINS;
    allInputs(true);
}

static Wiring checkWiring() {
    Wiring w = {};
    resetPins();

    // Pull-down read. Drive everything low briefly first: the RP2350's pull-down can otherwise hold
    // a floating pin up around 2 V (erratum E9).
    gpio_put_masked(ALL_PINS, 0);
    gpio_set_dir_out_masked(ALL_PINS);
    sleep_us(2);
    allInputs(false);
    sleep_us(100);
    w.highMask = gpio_get_all() & ALL_PINS;

    allInputs(true);
    sleep_us(100);
    w.lowMask = ~gpio_get_all() & ALL_PINS;
    uint32_t stuck = w.lowMask | w.highMask;

    for (int g = 0; g < 32; g++) {
        if (stuck & (1u << g)) {
            continue;
        }
        gpio_put(g, 0);
        gpio_set_dir(g, GPIO_OUT);
        sleep_us(30);
        w.conn[g] = ~gpio_get_all() & ALL_PINS & ~(1u << g) & ~stuck;
        gpio_set_dir(g, GPIO_IN);
        sleep_us(30);  // let the pull-up bring it (and whatever it touches) back up
    }

    // Which pair of shrouds? The one with the most straight-through lines, or failing that, the
    // ones with stuck pins.
    int best = 0;
    w.a = -1;
    for (int a = 0; a < SHROUDS; a++) {
        for (int b = a + 1; b < SHROUDS; b++) {
            int score = 0;
            for (int l = 0; l < LANES; l++) {
                if (w.conn[gpioOf(a, l)] & (1u << gpioOf(b, l))) {
                    score += 4;
                } else if (w.conn[gpioOf(a, l)] & (0xffu << (b * LANES))) {
                    score += 1;  // crossed, but still between these two
                }
                if (stuck & (1u << gpioOf(a, l))) score += 1;
                if (stuck & (1u << gpioOf(b, l))) score += 1;
            }
            if (score > best) {
                best = score;
                w.a = a;
                w.b = b;
            }
        }
    }
    uint32_t anyConn = 0;
    for (int g = 0; g < 32; g++) anyConn |= w.conn[g];
    w.present = best > 0 && (anyConn || stuck);
    if (!w.present) {
        return w;
    }

    for (int l = 0; l < LANES; l++) {
        int ga = gpioOf(w.a, l), gb = gpioOf(w.b, l);
        uint32_t expect = 1u << gb;
        w.other[l] = -1;
        if ((w.lowMask >> ga | w.lowMask >> gb) & 1) {
            w.fault[l] = L_GND;
        } else if ((w.highMask >> ga | w.highMask >> gb) & 1) {
            w.fault[l] = L_POWER;
        } else if (w.conn[ga] == expect) {
            w.fault[l] = L_OK;
            w.goodLanes |= 1u << l;
        } else if (!(w.conn[ga] & expect)) {
            uint32_t elsewhere = (w.conn[ga] >> (w.b * LANES)) & 0xff;
            if (elsewhere) {
                w.fault[l] = L_CROSSED;
                w.other[l] = __builtin_ctz(elsewhere);
            } else {
                w.fault[l] = L_OPEN;
            }
        } else {
            // Reaches its partner and something else too: a short to another line.
            uint32_t extra = w.conn[ga] & ~expect;
            uint32_t lanesA = (extra >> (w.a * LANES)) & 0xff;
            uint32_t lanesB = (extra >> (w.b * LANES)) & 0xff;
            uint32_t lanes = lanesA | lanesB;
            w.fault[l] = L_SHORTED;
            w.other[l] = lanes ? __builtin_ctz(lanes) : -1;
        }
    }
    return w;
}

static bool sameCable(const Wiring &x, const Wiring &y) {
    return x.present && y.present && x.a == y.a && x.b == y.b && x.goodLanes == y.goodLanes;
}

// ---------------------------------------------------------------------------------------------
// PIO loopback: out 8 lanes on one shroud, s cycles later sample 8 lanes on the other.

static uint16_t progInstr[2];
static pio_program prog;
static int progOffset = -1;

static void loadProgram(int s) {
    if (progOffset >= 0) {
        pio_remove_program(pio, &prog, progOffset);
    }
    int h = PERIOD - s;
    prog.instructions = progInstr;
    prog.length = 2;
    prog.origin = -1;
    progInstr[0] = pio_encode_out(pio_pins, 8) | pio_encode_delay(s - 1);
    progInstr[1] = pio_encode_in(pio_pins, 8) | pio_encode_delay(h - 1);
    progOffset = pio_add_program(pio, &prog);
}

static int curTx = -1, curRx = -1;
static uint8_t curDrive = 0;

static void setupLoopback(int txShroud, int rxShroud, uint8_t driveLanes) {
    resetPins();
    for (int l = 0; l < LANES; l++) {
        int gt = gpioOf(txShroud, l), gr = gpioOf(rxShroud, l);
        if (driveLanes & (1u << l)) {
            pio_gpio_init(pio, gt);
        }
        gpio_set_pulls(gr, true, false);
        pio->input_sync_bypass |= 1u << gr;
    }
    curTx = txShroud;
    curRx = rxShroud;
    curDrive = driveLanes;
}

// Runs n words (4n bit-times) of txbuf through the loopback with the sample s cycles after the
// edge; rxbuf gets what came back.
static void runLoopback(int s, int n) {
    loadProgram(s);
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, progOffset, progOffset + 1);
    sm_config_set_out_pins(&c, gpioOf(curTx, 0), 8);
    sm_config_set_in_pins(&c, gpioOf(curRx, 0));
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(pio, sm, progOffset, &c);
    // Only lanes that passed the wiring check are driven; the rest stay inputs.
    uint32_t txMask = (uint32_t) curDrive << gpioOf(curTx, 0);
    pio_sm_set_pins_with_mask(pio, sm, 0, txMask);
    pio_sm_set_pindirs_with_mask(pio, sm, txMask, (0xffu << gpioOf(curTx, 0)));
    pio_sm_clear_fifos(pio, sm);

    dma_channel_config cr = dma_channel_get_default_config(dmaRx);
    channel_config_set_transfer_data_size(&cr, DMA_SIZE_32);
    channel_config_set_read_increment(&cr, false);
    channel_config_set_write_increment(&cr, true);
    channel_config_set_dreq(&cr, pio_get_dreq(pio, sm, false));
    dma_channel_configure(dmaRx, &cr, rxbuf, &pio->rxf[sm], n, true);

    dma_channel_config ct = dma_channel_get_default_config(dmaTx);
    channel_config_set_transfer_data_size(&ct, DMA_SIZE_32);
    channel_config_set_read_increment(&ct, true);
    channel_config_set_write_increment(&ct, false);
    channel_config_set_dreq(&ct, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dmaTx, &ct, &pio->txf[sm], txbuf, n, true);

    pio_sm_set_enabled(pio, sm, true);
    dma_channel_wait_for_finish_blocking(dmaRx);
    pio_sm_set_enabled(pio, sm, false);
    dma_channel_abort(dmaTx);
}

// Counts mismatches per lane over n words; bit-time k of a word is byte k, lane l is bit l.
// The first word is skipped: the pins start each run at 0, so the first bit or two of a pattern
// that starts high (a victim held at 1, say) would read wrong at early sample points and look like
// crosstalk or slow edges on every line.
static void countErrors(int n, uint8_t lanes, uint32_t errs[LANES]) {
    uint32_t laneMask = lanes * 0x01010101u;
    for (int i = 1; i < n; i++) {
        uint32_t x = (txbuf[i] ^ rxbuf[i]) & laneMask;
        if (!x) continue;
        for (int l = 0; l < LANES; l++) {
            errs[l] += __builtin_popcount(x & (0x01010101u << l));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Patterns

enum PatKind { P_RANDOM, P_ALONE, P_QUIET0, P_QUIET1, P_OPPOSITE };

static uint32_t rng = 0x12345678;
static uint32_t rand32() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

// Fills n words; returns the lanes that are constant (quiet) in this pattern.
static uint8_t fillPattern(PatKind kind, int victim, uint8_t lanes, int n) {
    uint8_t v = 1u << victim;
    uint8_t agg = lanes & ~v;
    uint8_t orAll = 0, andAll = 0xff;
    for (int i = 0; i < n; i++) {
        uint32_t word = 0;
        for (int k = 0; k < 4; k++) {
            int t = i * 4 + k;
            bool phase = t & 1;
            uint8_t b;
            switch (kind) {
            case P_RANDOM: b = rand32() >> 24; break;
            case P_ALONE: b = (rand32() >> 24) & v; break;
            case P_QUIET0: b = phase ? agg : 0; break;
            case P_QUIET1: b = (phase ? agg : 0) | v; break;
            default: b = phase ? agg : v; break;  // P_OPPOSITE
            }
            b &= lanes;
            orAll |= b;
            andAll &= b;
            word |= (uint32_t) b << (8 * k);
        }
        txbuf[i] = word;
    }
    return lanes & ~(orAll ^ andAll);
}

// ---------------------------------------------------------------------------------------------
// The full test

struct DirResult {
    uint32_t togErr[LANES][S_MAX + 1];
    uint32_t quietErr[LANES][S_MAX + 1];
};
static DirResult dirRes[2];

static int steps;
static int totalSteps;

static void progress(const char *what) {
    printf("\r  %-22s %3d%%", what, steps * 100 / totalSteps);
    fflush(stdout);
}

static void sweep(int dir, int txS, int rxS, uint8_t lanes, const char *label) {
    DirResult &r = dirRes[dir];
    memset(&r, 0, sizeof r);
    setupLoopback(txS, rxS, lanes);
    for (int victim = -1; victim < LANES; victim++) {
        if (victim >= 0 && !(lanes & (1u << victim))) continue;
        int kinds = victim < 0 ? 1 : 4;
        for (int k = 0; k < kinds; k++) {
            PatKind kind = victim < 0 ? P_RANDOM : (PatKind) (P_ALONE + k);
            uint8_t quiet = fillPattern(kind, victim < 0 ? 0 : victim, lanes, TEST_WORDS);
            for (int s = S_MIN; s <= S_MAX; s++) {
                uint32_t e[LANES] = {};
                runLoopback(s, TEST_WORDS);
                countErrors(TEST_WORDS, lanes, e);
                for (int l = 0; l < LANES; l++) {
                    if (quiet & (1u << l)) {
                        r.quietErr[l][s] += e[l];
                    } else {
                        r.togErr[l][s] += e[l];
                    }
                }
            }
            steps++;
            progress(label);
        }
    }
}

// Earliest sample point from which there are no errors at all; S_MAX + 1 = never clean.
static int cleanFrom(const uint32_t errs[S_MAX + 1]) {
    int s = S_MAX + 1;
    while (s > S_MIN && errs[s - 1] == 0) s--;
    return s;
}

enum Grade { G_PASS, G_WEAK, G_FAIL };
static const char *gradeName[] = {"PASS", "WEAK", "FAIL"};

static Grade gradeOf(int s) { return s <= S_PASS ? G_PASS : s <= S_WEAK ? G_WEAK : G_FAIL; }

static void fmtTime(char *buf, int s) {
    if (s > S_MAX) {
        strcpy(buf, " never");
    } else if (s <= S_MIN) {
        snprintf(buf, 16, "<%3.0f ns", nsPerCycle * S_MIN);
    } else {
        snprintf(buf, 16, "%4.0f ns", nsPerCycle * s);
    }
}

static const char *faultText(LaneFault f) {
    switch (f) {
    case L_GND: return "SHORTED TO GND";
    case L_POWER: return "SHORTED TO 3V3/5V";
    case L_OPEN: return "OPEN (no connection)";
    case L_CROSSED: return "CROSSED to D";
    case L_SHORTED: return "SHORTED to D";
    default: return "ok";
    }
}

static int cablesTested = 0, cablesPassed = 0;
static bool lastPass = false;

static void banner(const char *text) {
    printf("\n==================================================================\n");
    printf("  %s\n", text);
    printf("==================================================================\n");
}

static bool fullTest(const Wiring &w) {
    cablesTested++;
    char title[96];
    snprintf(title, sizeof title, "CABLE #%d   J%d (OUT%d)  <->  J%d (OUT%d)", cablesTested,
             jNum(w.a), outNum(w.a), jNum(w.b), outNum(w.b));
    banner(title);

    int good = __builtin_popcount(w.goodLanes);
    printf("  wiring: %d/8 data lines straight through", good);
    printf(good == LANES ? ", no shorts\n" : "\n");
    uint32_t stuckBoth = 0;
    for (int l = 0; l < LANES; l++) {
        if (w.fault[l] == L_GND || w.fault[l] == L_POWER) stuckBoth++;
    }
    if (stuckBoth == LANES) {
        printf("  every data line is stuck: one connector is probably crimped mirror-image\n");
        printf("  (pin 1 stripe on the wrong side), so the data wires land on GND/power pins.\n");
    }

    steps = 0;
    totalSteps = 2 * (1 + 4 * good);
    if (good) {
        char label[32];
        snprintf(label, sizeof label, "speed J%d -> J%d", jNum(w.a), jNum(w.b));
        sweep(0, w.a, w.b, w.goodLanes, label);
        snprintf(label, sizeof label, "speed J%d -> J%d", jNum(w.b), jNum(w.a));
        sweep(1, w.b, w.a, w.goodLanes, label);
        printf("\r%40s\r", "");
    }

    printf("\n  line  pin   settle    xtalk     wiring / notes\n");
    printf("  ----  ---   -------   -------   ----------------------------\n");
    Grade worst = G_PASS;
    int fails = 0, weaks = 0;
    int fastest = S_MAX + 1;
    for (int l = 0; l < LANES; l++) {
        Grade g;
        char st[16] = "    --", xt[16] = "    --", note[64] = "";
        if (!(w.goodLanes & (1u << l))) {
            g = G_FAIL;
            if (w.other[l] >= 0) {
                snprintf(note, sizeof note, "%s%d", faultText(w.fault[l]), w.other[l] + 1);
            } else {
                snprintf(note, sizeof note, "%s", faultText(w.fault[l]));
            }
        } else {
            int settle = 0, xtalk = 0;
            for (int d = 0; d < 2; d++) {
                int s1 = cleanFrom(dirRes[d].togErr[l]);
                int s2 = cleanFrom(dirRes[d].quietErr[l]);
                if (s1 > settle) settle = s1;
                if (s2 > xtalk) xtalk = s2;
            }
            if (settle < fastest) fastest = settle;
            fmtTime(st, settle);
            fmtTime(xt, xtalk);
            Grade gs = gradeOf(settle), gx = gradeOf(xtalk);
            g = gs > gx ? gs : gx;
            if (gs != G_PASS) strcat(note, "slow edges ");
            if (gx != G_PASS) strcat(note, "crosstalk ");
            if (!note[0]) strcpy(note, "ok");
        }
        if (g > worst) worst = g;
        if (g == G_FAIL) fails++;
        if (g == G_WEAK) weaks++;
        printf("  D%d    %2d    %s   %s   %-28s %s%s\n", l + 1, 2 * (l + 1), st, xt, note,
               gradeName[g], g == G_PASS ? "" : "   <<<");
    }
    printf("\n  sweep: %.1f Mbit/s per line, sampled %.0f-%.0f ns after each edge; PASS < %.0f ns,"
           " WEAK < %.0f ns\n",
           clock_get_hz(clk_sys) / (float) PERIOD / 1e6f, nsPerCycle * S_MIN, nsPerCycle * S_MAX,
           nsPerCycle * (S_PASS + 1), nsPerCycle * (S_WEAK + 1));
    if (fastest <= S_MAX) {
        char ft[16];
        fmtTime(ft, fastest);
        printf("  fastest line:%s, which is about the chip's own out-to-in delay; a bad crimp is"
               " slower.\n", ft);
    }
    printf("  not tested here: GND (odd pins 1-11), 3V3 (13), 5V (15); check with a meter.\n");

    bool pass = worst == G_PASS;
    if (pass) cablesPassed++;
    printf("\n");
    if (pass) {
        printf("  >>>>>>>>>>>>>>>>>>>>>>>>   PASS   <<<<<<<<<<<<<<<<<<<<<<<<\n");
    } else if (worst == G_WEAK) {
        printf("  >>>>>>>>>>>>>>>>>   WEAK: %d line(s) marginal   <<<<<<<<<<<<<<<<<\n", weaks);
    } else {
        printf("  >>>>>>>>>>>>>>>>>   FAIL: %d line(s) bad   <<<<<<<<<<<<<<<<<<<<<\n", fails);
    }
    printf("  session: %d cable(s) tested, %d passed\n", cablesTested, cablesPassed);
    printf("\n  Soaking now: wiggle the cable at both connectors. Errors per line are counted\n");
    printf("  below. Unplug for the next cable, or press r to retest this one.\n\n");
    lastPass = pass;
    return pass;
}

// ---------------------------------------------------------------------------------------------
// Soak: until the cable comes out or a key is pressed.

static void fmtCount(char *buf, uint64_t n) {
    if (n < 10000) snprintf(buf, 8, "%5llu", (unsigned long long) n);
    else if (n < 10000000) snprintf(buf, 8, "%4lluk", (unsigned long long) (n / 1000));
    else snprintf(buf, 8, "%4lluM", (unsigned long long) (n / 1000000));
}

// Returns true if r was pressed (retest), false if the cable came out.
static bool soak(const Wiring &w) {
    uint64_t errs[LANES] = {};
    uint64_t bits = 0;
    absolute_time_t start = get_absolute_time();
    absolute_time_t nextPrint = make_timeout_time_ms(1000);
    uint8_t lanes = w.goodLanes;
    if (!lanes) {
        // Nothing to soak; just watch for the cable coming out.
        while (true) {
            int c = getchar_timeout_us(300000);
            if (c == 'r' || c == 'R') return true;
            Wiring now = checkWiring();
            if (!now.present) return false;
        }
    }
    int round = 0;
    while (true) {
        int dir = round & 1;
        PatKind kind = (round >> 1) & 1 ? P_OPPOSITE : P_RANDOM;
        int sample = (round >> 2) & 1 ? S_PASS : S_MAX;
        int victim = (round >> 3) % LANES;
        if (dir == 0) setupLoopback(w.a, w.b, lanes);
        else setupLoopback(w.b, w.a, lanes);
        fillPattern(kind, victim, lanes, SOAK_WORDS);
        uint32_t e[LANES] = {};
        // Sample at the PASS point and at the latest point: a good line is clean at both.
        runLoopback(sample, SOAK_WORDS);
        countErrors(SOAK_WORDS, lanes, e);
        bits += SOAK_WORDS * 4;
        int heavy = 0;
        for (int l = 0; l < LANES; l++) {
            errs[l] += e[l];
            if (e[l] > SOAK_WORDS) heavy++;  // > 25% wrong
        }
        round++;

        if (heavy > 0) {
            // Possibly unplugged; look before blaming the cable.
            Wiring now = checkWiring();
            if (!now.present) {
                printf("\n\n  cable removed.\n");
                return false;
            }
            if (!sameCable(now, w)) {
                printf("\n\n  wiring changed (%d/8 lines now), retesting.\n",
                       __builtin_popcount(now.goodLanes));
                return true;
            }
        }

        int c = getchar_timeout_us(0);
        if (c == 'r' || c == 'R') return true;

        if (absolute_time_diff_us(nextPrint, get_absolute_time()) >= 0) {
            nextPrint = delayed_by_ms(nextPrint, 1000);
            uint32_t secs = absolute_time_diff_us(start, get_absolute_time()) / 1000000;
            uint64_t total = 0;
            printf("\r  SOAK %2lu:%02lu  %s  errors D1-D8:", (unsigned long) (secs / 60),
                   (unsigned long) (secs % 60), lastPass ? "PASS" : "----");
            for (int l = 0; l < LANES; l++) {
                char buf[8];
                if (lanes & (1u << l)) fmtCount(buf, errs[l]);
                else strcpy(buf, "    -");
                printf(" %s", buf);
                total += errs[l];
            }
            char tb[8];
            fmtCount(tb, bits / 1000000);
            printf("   %sM bits %s", tb, total ? "  <<< ERRORS" : "  clean   ");
            fflush(stdout);
        }
    }
}

// ---------------------------------------------------------------------------------------------

static void waitingMessage() {
    printf("\n>>> WAITING FOR A CABLE: plug one ribbon between any two of J5-J8\n");
    printf("    (unplug the output boards first). %d tested, %d passed so far.\n", cablesTested,
           cablesPassed);
}

// What the wiring check saw on every pin, for when a cable isn't recognised. One character per
// data line D1-D8: '.' nothing, 'L' stuck low (GND), 'H' stuck high (power), or the J number of the
// shroud whose pin follows this one ('*' if it follows pins on more than one shroud).
static void dumpRaw(const Wiring &w) {
    printf("    pins D1-D8:");
    for (int sh = 0; sh < SHROUDS; sh++) {
        printf("  J%d ", jNum(sh));
        for (int l = 0; l < LANES; l++) {
            int g = gpioOf(sh, l);
            char c = '.';
            if (w.lowMask & (1u << g)) {
                c = 'L';
            } else if (w.highMask & (1u << g)) {
                c = 'H';
            } else if (w.conn[g]) {
                int first = __builtin_ctz(w.conn[g]) / LANES;
                int last = (31 - __builtin_clz(w.conn[g])) / LANES;
                c = first == last ? (char) ('0' + jNum(first)) : '*';
            }
            putchar(c);
        }
    }
    printf("\n");
}

int main() {
    stdio_init_all();
    sm = pio_claim_unused_sm(pio, true);
    dmaTx = dma_claim_unused_channel(true);
    dmaRx = dma_claim_unused_channel(true);
    nsPerCycle = 1e9f / clock_get_hz(clk_sys);
    rng ^= time_us_32();
    if (!rng) rng = 1;
    resetPins();

    sleep_ms(1500);  // give tio a moment to reconnect after flashing
    banner("CABLE TEST  -  IDC ribbon loopback, J5-J8");
    printf("  Speed test %.1f Mbit/s per line (LEDs run at 0.8), both directions,\n",
           clock_get_hz(clk_sys) / (float) PERIOD / 1e6f);
    printf("  plus crosstalk and a soak you can wiggle the cable during.\n");
    waitingMessage();

    absolute_time_t nextReminder = make_timeout_time_ms(10000);
    while (true) {
        Wiring w = checkWiring();
        if (!w.present) {
            if (absolute_time_diff_us(nextReminder, get_absolute_time()) >= 0) {
                waitingMessage();
                dumpRaw(w);
                nextReminder = make_timeout_time_ms(10000);
            }
            int c = getchar_timeout_us(200000);
            if (c == 'd' || c == 'D') {
                dumpRaw(w);
            }
            continue;
        }
        // Give both ends a moment to seat, then make sure we see the same thing twice.
        sleep_ms(700);
        Wiring w2 = checkWiring();
        if (!sameCable(w, w2)) {
            continue;
        }
        while (true) {
            fullTest(w2);
            if (!soak(w2)) break;
            w2 = checkWiring();
            if (!w2.present) break;
        }
        waitingMessage();
        nextReminder = make_timeout_time_ms(10000);
    }
}
