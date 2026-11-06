// PSRAM stress test for the processor board's U4 (APS6404L, 8 MB, QMI chip select 1 on GPIO47).
//
// Runs passes forever, alternating the system clock between 150 MHz and PSRAM_TEST_FAST_KHZ
// (200 MHz by default), re-timing the PSRAM for each clock. Each pass:
//   1. data bus     walking ones and zeros on all 32 bits
//   2. address      a marker at every power-of-two offset, checked for wraps and aliases
//   3. byte lanes   byte and halfword stores read back as words
//   4. fill u/u     all 8 MB written and checked uncached, with a new seed every pass
//   5. fill c/u     all 8 MB written through the cache, cache cleaned, checked uncached
//   6. fill u/c     all 8 MB written uncached, cache invalidated, checked through the cache
//   7. dma          all 8 MB SRAM -> PSRAM -> SRAM by DMA, 16 KB at a time
//   8. two cores    core 0 fills and checks the low 4 MB while core 1 does the high 4 MB, both
//                   running code from flash, so the flash and the PSRAM fight over the QMI bus
// then prints speeds and a verdict big enough to read across the bench:
//   >>> PASS 12 @ 200 MHz: OK   total bad words 0
// Bad words get a line each (first 8 per pass) with the offset, what was written, what came back,
// and which bits differ: one bit always wrong points at a QSPI data line (SD0-SD3) joint.
//
// At boot it also checks the linker's PSRAM sections: an initialised __in_psram variable must arrive
// with its value, and an __uninitialized_psram buffer must land in the PSRAM window.
//
// No GPIO 0-31 are touched, so output boards can stay plugged in. USB serial only.
#include <cstdio>

#include "hardware/clocks.h"
#include "hardware/psram.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "psram_stress.h"

#ifndef PSRAM_TEST_FAST_KHZ
#define PSRAM_TEST_FAST_KHZ 200000
#endif

using namespace psram_stress;

// Linker placement checks. The SDK copies __in_psram data from flash into the PSRAM at boot.
static uint32_t __in_psram("psram_test") loadedCheck[4] = {0xc0ffee01, 0xdeadbeef, 0x12345678, 0x0badf00d};
static uint8_t __uninitialized_psram("psram_test") bigBuffer[1024 * 1024];

static bool sectionsOk = false;
static uint32_t loadedSeen[4];

static void checkSections() {
    const uint32_t want[4] = {0xc0ffee01, 0xdeadbeef, 0x12345678, 0x0badf00d};
    sectionsOk = true;
    for (int i = 0; i < 4; i++) {
        loadedSeen[i] = loadedCheck[i];
        if (loadedSeen[i] != want[i]) sectionsOk = false;
    }
    if (!psram_check_address(bigBuffer) || !psram_check_address(bigBuffer + sizeof(bigBuffer) - 1)) {
        sectionsOk = false;
    }
}

static void printSections() {
    printf("  sections: __in_psram at %p = %08lx %08lx %08lx %08lx, 1 MB buffer at %p: %s\n",
           (void *)loadedCheck, (unsigned long)loadedSeen[0], (unsigned long)loadedSeen[1],
           (unsigned long)loadedSeen[2], (unsigned long)loadedSeen[3], (void *)bigBuffer,
           sectionsOk ? "ok" : "BAD");
}

// Change clk_sys and re-time the PSRAM for it. Interrupts are off and core 1 is parked, because
// psram_reinitialize() briefly takes the QMI out of XIP mode and nothing may run from flash then.
static void setClock(uint32_t khz) {
    if (khz > 200000) {
        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(10);
    }
    uint32_t ints = save_and_disable_interrupts();
    set_sys_clock_khz(khz, true);
    psram_configure_params(PICO_DEFAULT_PSRAM_MAX_FREQ, PICO_DEFAULT_PSRAM_MAX_SELECT,
                           PICO_DEFAULT_PSRAM_MIN_DESELECT);
    psram_reinitialize();
    restore_interrupts(ints);
    if (khz <= 200000) {
        vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
    }
}

// Core 1's half of the two-core stage.
static volatile uint32_t core1Seed = 0;
static volatile uint32_t core1Errors = 0;
static volatile bool core1Done = false;

static void core1Main() {
    uint32_t half = static_cast<uint32_t>(size() / 2);
    core1Errors = fillVerify(half, half, core1Seed, false, false);
    core1Done = true;
    while (true) tight_loop_contents();
}

static uint32_t twoCores(uint32_t seed) {
    uint32_t half = static_cast<uint32_t>(size() / 2);
    core1Seed = seed ^ 0x5a5a5a5a;
    core1Done = false;
    multicore_reset_core1();
    multicore_launch_core1(core1Main);
    uint32_t errors = fillVerify(0, half, seed, false, false);
    while (!core1Done) tight_loop_contents();
    multicore_reset_core1();
    return errors + core1Errors;
}

static void step(const char *name, uint32_t errors, uint64_t startUs) {
    printf("  %-10s %s  (%.2f s)\n", name, errors ? "*** BAD ***" : "ok", (time_us_64() - startUs) / 1e6);
    if (errors) {
        printf("    %lu bad words\n", (unsigned long)errors);
        printLog(8);
    }
}

int main() {
    // Before stdio: nothing has touched the PSRAM yet, so the boot-time copy is what we see.
    bool havePsram = present();
    if (havePsram) checkSections();

    stdio_init_all();
    // Give a terminal a moment to attach so the first report isn't lost.
    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);

    if (!havePsram) {
        while (true) {
            printf("\n>>> NO PSRAM: U4 didn't answer its ID read at boot\n");
            printf("    check U4's joints (pin 1 CS = GPIO47 via R11, SCLK, SD0-SD3), C22, and R13\n");
            sleep_ms(3000);
        }
    }

    clearLog();  // sets up the error log's lock before core 1 can use it

    const uint32_t clocks[2] = {150000, PSRAM_TEST_FAST_KHZ};
    uint32_t pass = 0;
    uint32_t badPasses = 0;
    while (true) {
        pass++;
        uint32_t khz = clocks[(pass - 1) % 2];
        setClock(khz);
        uint32_t seed = pass * 0x01000193u;
        uint32_t sz = static_cast<uint32_t>(size());
        uint32_t before = totalErrors();
        uint64_t passStart = time_us_64();

        printf("\n=== PSRAM pass %lu @ %lu MHz (measured %lu MHz), %lu MB, seed %08lx ===\n",
               (unsigned long)pass, (unsigned long)(khz / 1000),
               (unsigned long)(frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000),
               (unsigned long)(sz >> 20), (unsigned long)seed);
        printSections();

        uint64_t t = time_us_64();
        step("data bus", dataBus(), t);
        t = time_us_64();
        step("address", addressLines(), t);
        t = time_us_64();
        step("byte lanes", byteLanes(), t);
        t = time_us_64();
        step("fill u/u", fillVerify(0, sz, seed, false, false), t);
        t = time_us_64();
        step("fill c/u", fillVerify(0, sz, ~seed, true, false), t);
        t = time_us_64();
        step("fill u/c", fillVerify(0, sz, seed + 1, false, true), t);
        t = time_us_64();
        step("dma", dmaRoundTrip(0, sz, seed + 2), t);
        t = time_us_64();
        step("two cores", twoCores(seed + 3), t);

        Bench b = bench();
        printf("  speed MB/s: write %.1f  read %.1f  cached read %.1f  memcpy->SRAM %.1f  "
               "DMA in %.1f  out %.1f\n",
               b.writeUncached, b.readUncached, b.readCached, b.memcpyToSram, b.dmaToPsram, b.dmaFromPsram);

        uint32_t bad = totalErrors() - before;
        if (bad) badPasses++;
        printf(">>> PASS %lu @ %lu MHz: %s   bad words this pass %lu, total %lu, bad passes %lu/%lu  (%.1f s)\n",
               (unsigned long)pass, (unsigned long)(khz / 1000), bad ? "*** FAIL ***" : "OK",
               (unsigned long)bad, (unsigned long)totalErrors(), (unsigned long)badPasses,
               (unsigned long)pass, (time_us_64() - passStart) / 1e6);
    }
}
