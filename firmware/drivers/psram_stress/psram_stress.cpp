#include "psram_stress.h"

#include <cstdio>
#include <cstring>

#include "hardware/dma.h"
#include "hardware/psram.h"
#include "hardware/xip_cache.h"
#include "pico/critical_section.h"
#include "pico/stdlib.h"

namespace psram_stress {

namespace {

constexpr int kLogSize = 16;

struct LogEntry {
    const char *test;
    uint32_t offset;
    uint32_t expected;
    uint32_t got;
};

LogEntry logEntries[kLogSize];
int logCount = 0;
uint32_t errorTotal = 0;
critical_section_t logLock;
bool logLockReady = false;

void ensureLock() {
    // First call is from core 0 before core 1 runs anything, so no race on the init itself.
    if (!logLockReady) {
        critical_section_init(&logLock);
        logLockReady = true;
    }
}

void logError(const char *test, uint32_t offset, uint32_t expected, uint32_t got) {
    ensureLock();
    critical_section_enter_blocking(&logLock);
    errorTotal++;
    if (logCount < kLogSize) {
        logEntries[logCount++] = {test, offset, expected, got};
    }
    critical_section_exit(&logLock);
}

inline volatile uint32_t *word(uintptr_t base, uint32_t offset) {
    return reinterpret_cast<volatile uint32_t *>(base + offset);
}

// The XIP cache maintenance calls work on the whole cache: flash lines get cleaned and dropped
// too, which just costs some refetching.
void cleanCache() { xip_cache_clean_all(); }
void invalidateCache() {
    xip_cache_clean_all();
    xip_cache_invalidate_all();
}

constexpr uint32_t kBlock = 16 * 1024;
uint32_t sramA[kBlock / 4];
uint32_t sramB[kBlock / 4];

void dmaCopy(int ch, void *dst, const void *src, uint32_t bytes) {
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(ch, &c, dst, src, bytes / 4, true);
    dma_channel_wait_for_finish_blocking(ch);
}

float mbps(uint32_t bytes, uint64_t us) { return us ? bytes / static_cast<float>(us) : 0.0f; }

}  // namespace

bool present() { return psram_is_available(); }

size_t size() { return psram_get_size(); }

uint32_t pattern(uint32_t offset, uint32_t seed) {
    // A 32-bit integer hash (lowbias32): every input bit flips about half the output bits.
    uint32_t x = offset ^ (seed * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t dataBus() {
    uint32_t errors = 0;
    const uint32_t offsets[] = {0, 0x1234, static_cast<uint32_t>(size() / 2) + 0x40,
                                static_cast<uint32_t>(size()) - 4};
    for (uint32_t off : offsets) {
        volatile uint32_t *p = word(kUncachedBase, off);
        for (int bit = 0; bit < 32; bit++) {
            for (int inv = 0; inv < 2; inv++) {
                uint32_t v = 1u << bit;
                if (inv) v = ~v;
                *p = v;
                uint32_t got = *p;
                if (got != v) {
                    errors++;
                    logError("data bus", off, v, got);
                }
            }
        }
    }
    return errors;
}

uint32_t addressLines() {
    uint32_t errors = 0;
    uint32_t sz = static_cast<uint32_t>(size());
    // Offsets 0, 4, 8, 16, ... sz/2. Write all of them, then check all of them.
    const uint32_t marker = 0xa5000000u;
    *word(kUncachedBase, 0) = marker;
    for (uint32_t off = 4; off < sz; off <<= 1) {
        *word(kUncachedBase, off) = marker | off >> 2;
    }
    if (*word(kUncachedBase, 0) != marker) {
        errors++;
        logError("address", 0, marker, *word(kUncachedBase, 0));
    }
    for (uint32_t off = 4; off < sz; off <<= 1) {
        uint32_t want = marker | off >> 2;
        uint32_t got = *word(kUncachedBase, off);
        if (got != want) {
            errors++;
            logError("address", off, want, got);
        }
    }
    return errors;
}

uint32_t byteLanes() {
    uint32_t errors = 0;
    const uint32_t base = static_cast<uint32_t>(size() / 4) + 0x100;  // somewhere off the beaten path
    volatile uint8_t *b = reinterpret_cast<volatile uint8_t *>(kUncachedBase + base);
    volatile uint16_t *h = reinterpret_cast<volatile uint16_t *>(kUncachedBase + base + 64);
    for (int i = 0; i < 64; i++) {
        b[i] = static_cast<uint8_t>(0x11 * (i & 15) ^ i);
    }
    for (int i = 0; i < 32; i++) {
        h[i] = static_cast<uint16_t>(0x1357 * (i + 1));
    }
    for (int w = 0; w < 16; w++) {
        uint32_t want = 0;
        for (int k = 0; k < 4; k++) {
            int i = w * 4 + k;
            want |= static_cast<uint32_t>(static_cast<uint8_t>(0x11 * (i & 15) ^ i)) << (8 * k);
        }
        uint32_t got = *word(kUncachedBase, base + w * 4);
        if (got != want) {
            errors++;
            logError("byte lanes", base + w * 4, want, got);
        }
    }
    for (int w = 0; w < 16; w++) {
        uint32_t want = static_cast<uint16_t>(0x1357 * (2 * w + 1)) |
                        static_cast<uint32_t>(static_cast<uint16_t>(0x1357 * (2 * w + 2))) << 16;
        uint32_t got = *word(kUncachedBase, base + 64 + w * 4);
        if (got != want) {
            errors++;
            logError("halfwords", base + 64 + w * 4, want, got);
        }
    }
    return errors;
}

uint32_t fillVerify(uint32_t offset, uint32_t len, uint32_t seed, bool writeCached, bool readCached) {
    const char *name = writeCached ? (readCached ? "fill c/c" : "fill c/u") : (readCached ? "fill u/c" : "fill u/u");
    uintptr_t wbase = writeCached ? kCachedBase : kUncachedBase;
    uintptr_t rbase = readCached ? kCachedBase : kUncachedBase;
    for (uint32_t off = offset; off < offset + len; off += 4) {
        *word(wbase, off) = pattern(off, seed);
    }
    if (writeCached) cleanCache();
    if (readCached) invalidateCache();
    uint32_t errors = 0;
    for (uint32_t off = offset; off < offset + len; off += 4) {
        uint32_t want = pattern(off, seed);
        uint32_t got = *word(rbase, off);
        if (got != want) {
            errors++;
            logError(name, off, want, got);
        }
    }
    return errors;
}

uint32_t dmaRoundTrip(uint32_t offset, uint32_t len, uint32_t seed) {
    int ch = dma_claim_unused_channel(true);
    uint32_t errors = 0;
    for (uint32_t block = offset; block < offset + len; block += kBlock) {
        for (uint32_t i = 0; i < kBlock / 4; i++) {
            sramA[i] = pattern(block + i * 4, seed);
        }
        memset(sramB, 0, sizeof(sramB));
        dmaCopy(ch, reinterpret_cast<void *>(kUncachedBase + block), sramA, kBlock);
        dmaCopy(ch, sramB, reinterpret_cast<const void *>(kUncachedBase + block), kBlock);
        for (uint32_t i = 0; i < kBlock / 4; i++) {
            if (sramB[i] != sramA[i]) {
                errors++;
                logError("dma", block + i * 4, sramA[i], sramB[i]);
            }
        }
    }
    dma_channel_unclaim(ch);
    return errors;
}

Bench bench() {
    constexpr uint32_t kBytes = 1024 * 1024;
    Bench r{};
    volatile uint32_t sink = 0;

    uint64_t t = time_us_64();
    for (uint32_t off = 0; off < kBytes; off += 4) *word(kUncachedBase, off) = off;
    r.writeUncached = mbps(kBytes, time_us_64() - t);

    t = time_us_64();
    uint32_t acc = 0;
    for (uint32_t off = 0; off < kBytes; off += 4) acc += *word(kUncachedBase, off);
    r.readUncached = mbps(kBytes, time_us_64() - t);

    invalidateCache();
    t = time_us_64();
    for (uint32_t off = 0; off < kBytes; off += 4) acc += *word(kCachedBase, off);
    r.readCached = mbps(kBytes, time_us_64() - t);

    invalidateCache();
    t = time_us_64();
    for (uint32_t off = 0; off < kBytes; off += kBlock) {
        memcpy(sramB, reinterpret_cast<const void *>(kCachedBase + off), kBlock);
    }
    r.memcpyToSram = mbps(kBytes, time_us_64() - t);
    sink = acc + sramB[0];

    int ch = dma_claim_unused_channel(true);
    t = time_us_64();
    for (uint32_t off = 0; off < kBytes; off += kBlock) {
        dmaCopy(ch, reinterpret_cast<void *>(kUncachedBase + off), sramA, kBlock);
    }
    r.dmaToPsram = mbps(kBytes, time_us_64() - t);
    t = time_us_64();
    for (uint32_t off = 0; off < kBytes; off += kBlock) {
        dmaCopy(ch, sramB, reinterpret_cast<const void *>(kUncachedBase + off), kBlock);
    }
    r.dmaFromPsram = mbps(kBytes, time_us_64() - t);
    dma_channel_unclaim(ch);
    (void)sink;
    return r;
}

uint32_t totalErrors() {
    ensureLock();
    critical_section_enter_blocking(&logLock);
    uint32_t n = errorTotal;
    critical_section_exit(&logLock);
    return n;
}

void clearLog() {
    ensureLock();
    critical_section_enter_blocking(&logLock);
    logCount = 0;
    critical_section_exit(&logLock);
}

void printLog(int max) {
    ensureLock();
    LogEntry copy[kLogSize];
    critical_section_enter_blocking(&logLock);
    int n = logCount;
    memcpy(copy, logEntries, sizeof(copy));
    logCount = 0;
    critical_section_exit(&logLock);
    for (int i = 0; i < n && i < max; i++) {
        uint32_t diff = copy[i].expected ^ copy[i].got;
        printf("    BAD %-10s at 0x%06lx: wrote %08lx read %08lx (bits %08lx)\n", copy[i].test,
               static_cast<unsigned long>(copy[i].offset), static_cast<unsigned long>(copy[i].expected),
               static_cast<unsigned long>(copy[i].got), static_cast<unsigned long>(diff));
    }
}

}  // namespace psram_stress
