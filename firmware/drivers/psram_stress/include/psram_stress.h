// PSRAM stress tests for the processor board's APS6404L (U4, 8 MB QSPI PSRAM on the RP2350's
// QMI chip select 1, GPIO47).
//
// The SDK (2.3.1's hardware_psram) brings the chip up at boot: link hardware_psram and it reads
// the chip's ID, sets the QMI timing for the current clock and maps it at 0x11000000 (cached, shares
// the 16 KB XIP cache with flash) and 0x15000000 (uncached). These tests hammer both windows.
//
// Every test returns the number of bad words it found and logs the first few (test, offset,
// expected, got) for printLog(). The log is safe to add to from both cores.
#ifndef PSRAM_STRESS_H
#define PSRAM_STRESS_H

#include <cstddef>
#include <cstdint>

namespace psram_stress {

constexpr uintptr_t kCachedBase = 0x11000000;    // XIP_BASE + 16 MB: cached, read/write
constexpr uintptr_t kUncachedBase = 0x15000000;  // XIP_NOCACHE_NOALLOC_BASE + 16 MB

// True if the SDK found the chip at boot (by its ID, with PICO_AUTO_DETECT_PSRAM_SIZE).
bool present();
// Bytes the SDK mapped, 0 if none.
size_t size();

// The word written at a byte offset for a given seed. Different for every word and every seed, so a
// stuck bit, a wrapped address or a stale cache line all show up as a mismatch.
uint32_t pattern(uint32_t offset, uint32_t seed);

// Walking ones and zeros on all 32 data bits, at a few spread-out offsets. Uncached.
uint32_t dataBus();
// Writes a marker at offset 0 and at every power-of-two offset up to the size, then checks no
// write landed on another (a short address or a wrong size wraps). Uncached.
uint32_t addressLines();
// Byte and halfword writes, read back as words, so each byte lane is checked on its own. Uncached.
uint32_t byteLanes();
// Fills [offset, offset + len) through one window and checks it through another. Writing through
// the cached window cleans the cache before checking, so the data really went to the chip; reading
// through it invalidates first, so it really comes from the chip.
uint32_t fillVerify(uint32_t offset, uint32_t len, uint32_t seed, bool writeCached, bool readCached);
// DMA from SRAM to PSRAM and back, one block at a time over [offset, offset + len).
uint32_t dmaRoundTrip(uint32_t offset, uint32_t len, uint32_t seed);

// MB/s for 1 MB moves (MB = 10^6 bytes).
struct Bench {
    float writeUncached;  // CPU word stores, uncached
    float readUncached;   // CPU word loads, uncached
    float readCached;     // CPU word loads through the cache (all misses: 1 MB >> 16 KB cache)
    float memcpyToSram;   // memcpy, cached PSRAM -> SRAM
    float dmaToPsram;     // DMA SRAM -> uncached PSRAM
    float dmaFromPsram;   // DMA uncached PSRAM -> SRAM
};
Bench bench();

// Error log.
uint32_t totalErrors();
void clearLog();
// Prints up to max logged errors, then forgets them (the total keeps counting).
void printLog(int max = 8);

}  // namespace psram_stress

#endif  // PSRAM_STRESS_H
