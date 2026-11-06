// Uber-test: everything on the processor board at once, for a long soak on the bench.
//
// Core 1 owns the radio (as the show firmware will): brings up the RM2 with bringup's retries,
// joins WiFi, keeps the link up (reconnects if it drops), and gets NTP time every 15 minutes.
// D1 blinks with J2's GP35 once the radio is up.
//
// Core 0 does the rest:
// - The RTC: every NTP answer is written to the DS3231 (with how far off it was), and the clock is
//   shown in US Eastern time on every status line.
// - The LEDs, the way the show should use the PSRAM: 100 frames of a slow wave (OUT1 gold, OUT2
//   red, OUT3 green, OUT4 warm white, so each shroud is easy to tell apart) are built once at boot
//   and stored in the PSRAM in the PIO's wire format. Each frame is DMA'd from the PSRAM into one
//   of two SRAM buffers while the other goes out on GPIO 0-31, 20 frames a second. Every frame is
//   checked against a checksum taken when it was built, so the PSRAM is tested by real use.
// - A background PSRAM stress test on the PSRAM the frames don't use (32 KB a frame, through the
//   cache and around it, new pattern every sweep).
// - Once a second a status line, every 30 s the full report.
//
// WiFi credentials: built in from firmware/wifi_secrets.cmake if set; otherwise the board asks for
// them over USB serial and keeps them in the last 4 KB of flash, so a shared .uf2 carries none.
// Press w in the terminal at any time to enter new ones (the board reboots to ask).
//
// Strands on GPIO 0-31 light at brightness UBER_BRIGHTNESS/256 (16 by default, like all_strands).
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"

#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "ds3231.hpp"
#include "psram_stress.h"
#include "rtc_local_time.h"
#include "rtc_pins.h"
#include "ws2811_parallel.pio.h"

#ifndef UBER_BRIGHTNESS
#define UBER_BRIGHTNESS 16
#endif
#ifndef UBER_LEDS
#define UBER_LEDS 1
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#define HEARTBEAT_PIN 35
#define RADIO_RETRY_MS 500
#define RADIO_MAX_TRIES 60
#define NTP_EVERY_MS (15 * 60 * 1000)

using namespace psram_stress;

// ---------------------------------------------------------------------------------------------
// WiFi credentials in the last flash sector.

#define CREDS_MAGIC 0x49464957u  // "WIFI"
#define CREDS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETUP_SCRATCH_MAGIC 0x5e70f1f1u

struct Creds {
    uint32_t magic;
    char ssid[33];
    char pass[65];
    uint32_t check;
};
static_assert(sizeof(Creds) <= FLASH_PAGE_SIZE, "creds must fit one flash page");

static Creds creds;

static uint32_t credsCheck(const Creds &c) {
    uint32_t h = 2166136261u;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&c);
    for (size_t i = 0; i < offsetof(Creds, check); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static bool loadCreds() {
    if (strlen(WIFI_SSID) > 0) {
        memset(&creds, 0, sizeof(creds));
        strncpy(creds.ssid, WIFI_SSID, sizeof(creds.ssid) - 1);
        strncpy(creds.pass, WIFI_PASSWORD, sizeof(creds.pass) - 1);
        return true;
    }
    memcpy(&creds, reinterpret_cast<const void *>(XIP_BASE + CREDS_OFFSET), sizeof(creds));
    if (creds.magic == CREDS_MAGIC && creds.check == credsCheck(creds)) {
        creds.ssid[sizeof(creds.ssid) - 1] = 0;
        creds.pass[sizeof(creds.pass) - 1] = 0;
        return true;
    }
    memset(&creds, 0, sizeof(creds));
    return false;
}

// Only while core 1 isn't running: nothing may execute from flash during the erase and program.
static void saveCreds() {
    creds.magic = CREDS_MAGIC;
    creds.check = credsCheck(creds);
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &creds, sizeof(creds));
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CREDS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CREDS_OFFSET, page, sizeof(page));
    restore_interrupts(ints);
}

// Reads a line from USB serial with echo and backspace. Returns false on timeout (0 = never).
static bool readLine(char *buf, size_t size, uint32_t timeoutMs, bool hide) {
    size_t n = 0;
    absolute_time_t deadline = timeoutMs ? make_timeout_time_ms(timeoutMs) : at_the_end_of_time;
    while (!time_reached(deadline)) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;
            buf[n] = 0;
            printf("\n");
            return true;
        }
        if ((c == 8 || c == 127) && n > 0) {
            n--;
            printf("\b \b");
        } else if (c >= 32 && c < 127 && n + 1 < size) {
            buf[n++] = static_cast<char>(c);
            putchar(hide ? '*' : c);
        }
    }
    return false;
}

static void askForCreds(uint32_t timeoutMs) {
    printf("\n>>> WIFI SETUP: type the network name and press Enter%s\n",
           timeoutMs ? " (or wait to skip WiFi)" : "");
    printf("SSID: ");
    Creds c{};
    if (!readLine(c.ssid, sizeof(c.ssid), timeoutMs, false)) {
        printf("\n  no SSID entered, running without WiFi (press w later to set it)\n");
        return;
    }
    printf("Password: ");
    if (!readLine(c.pass, sizeof(c.pass), 0, true)) return;
    creds = c;
    saveCreds();
    printf("  saved \"%s\" to flash\n", creds.ssid);
}

// ---------------------------------------------------------------------------------------------
// Core 1: radio, WiFi, NTP.

enum class Net { RADIO_STARTING, NO_RM2, NO_CREDS, JOINING, UP, JOIN_FAILED };

struct NetShared {
    volatile Net state = Net::RADIO_STARTING;
    volatile int radioTries = 0;
    volatile int radioError = 0;
    volatile int lastJoinError = 0;
    volatile int32_t rssi = 0;
    volatile uint32_t joins = 0;
    volatile uint32_t drops = 0;
    volatile uint32_t ntpOk = 0;
    volatile uint32_t ntpFail = 0;
    volatile uint32_t ntpSeq = 0;      // bumped on each NTP answer
    volatile int64_t ntpEpoch = 0;     // Unix seconds from that answer
    volatile uint32_t ntpAtMs = 0;     // when it came
    char ip[20] = "";
};
static NetShared net;

constexpr char kNtpServer[] = "pool.ntp.org";
constexpr size_t kNtpMsgLen = 48;
constexpr uint16_t kNtpPort = 123;
constexpr uint32_t kNtpDelta = 2208988800u;
constexpr uint32_t kNtpTimeoutMs = 10 * 1000;

struct NtpState {
    ip_addr_t server;
    udp_pcb *pcb;
    volatile bool done;
    volatile bool ok;
    time_t epoch;
};

static void ntpSend(NtpState *s) {
    cyw43_arch_lwip_begin();
    pbuf *p = pbuf_alloc(PBUF_TRANSPORT, kNtpMsgLen, PBUF_RAM);
    uint8_t *req = static_cast<uint8_t *>(p->payload);
    memset(req, 0, kNtpMsgLen);
    req[0] = 0x1b;
    udp_sendto(s->pcb, p, &s->server, kNtpPort);
    pbuf_free(p);
    cyw43_arch_lwip_end();
}

static void ntpDns(const char *, const ip_addr_t *addr, void *arg) {
    auto *s = static_cast<NtpState *>(arg);
    if (addr) {
        s->server = *addr;
        ntpSend(s);
    } else {
        s->done = true;
    }
}

static void ntpRecv(void *arg, udp_pcb *, pbuf *p, const ip_addr_t *addr, u16_t port) {
    auto *s = static_cast<NtpState *>(arg);
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);
    if (ip_addr_cmp(addr, &s->server) && port == kNtpPort && p->tot_len == kNtpMsgLen && mode == 4 &&
        stratum != 0) {
        uint8_t b[4] = {};
        pbuf_copy_partial(p, b, sizeof(b), 40);
        uint32_t since1900 = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
        s->epoch = static_cast<time_t>(since1900 - kNtpDelta);
        s->ok = true;
    }
    s->done = true;
    pbuf_free(p);
}

static bool ntpOnce(time_t &epoch) {
    NtpState s{};
    cyw43_arch_lwip_begin();
    s.pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (s.pcb) udp_recv(s.pcb, ntpRecv, &s);
    int err = s.pcb ? dns_gethostbyname(kNtpServer, &s.server, ntpDns, &s) : ERR_MEM;
    cyw43_arch_lwip_end();
    if (!s.pcb) return false;
    if (err == ERR_OK) {
        ntpSend(&s);
    } else if (err != ERR_INPROGRESS) {
        s.done = true;
    }
    absolute_time_t deadline = make_timeout_time_ms(kNtpTimeoutMs);
    while (!s.done && !time_reached(deadline)) sleep_ms(50);
    cyw43_arch_lwip_begin();
    udp_remove(s.pcb);
    cyw43_arch_lwip_end();
    epoch = s.epoch;
    return s.done && s.ok;
}

static bool radioUp() {
    int err = cyw43_arch_init();
    while (err == 0) {
        net.radioTries = net.radioTries + 1;
        err = cyw43_gpio_set(&cyw43_state, CYW43_WL_GPIO_LED_PIN, false);
        if (err == 0 || net.radioTries >= RADIO_MAX_TRIES) break;
        // A failed bring-up leaves the gSPI PIO claimed; release it before the next try (as bringup).
        cyw43_thread_enter();
        cyw43_ll_deinit(&cyw43_state.cyw43_ll);
        cyw43_thread_exit();
        sleep_ms(RADIO_RETRY_MS);
    }
    net.radioError = err;
    return err == 0;
}

static void followHeartbeat() {
    static bool led = false;
    bool on = gpio_get_out_level(HEARTBEAT_PIN);
    if (on != led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
        led = on;
    }
}

// Sleeps ms while keeping D1 in step with GP35.
static void netSleep(uint32_t ms) {
    absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) {
        followHeartbeat();
        sleep_ms(10);
    }
}

static void core1Main() {
    if (!radioUp()) {
        net.state = Net::NO_RM2;
        return;
    }
    if (creds.ssid[0] == 0) {
        net.state = Net::NO_CREDS;
        while (true) netSleep(1000);
    }
    cyw43_arch_enable_sta_mode();
    uint32_t lastNtpMs = 0;
    bool haveNtp = false;
    while (true) {
        if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
            if (net.state == Net::UP) net.drops = net.drops + 1;
            net.state = Net::JOINING;
            net.ip[0] = 0;
            int err = cyw43_arch_wifi_connect_timeout_ms(creds.ssid, creds.pass, CYW43_AUTH_WPA2_AES_PSK, 30000);
            if (err) {
                net.lastJoinError = err;
                net.state = Net::JOIN_FAILED;
                netSleep(10000);
                continue;
            }
            net.joins = net.joins + 1;
            net.state = Net::UP;
            haveNtp = false;
            cyw43_arch_lwip_begin();
            snprintf(net.ip, sizeof(net.ip), "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
            cyw43_arch_lwip_end();
        }
        int32_t rssi = 0;
        if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) net.rssi = rssi;

        uint32_t nowMs = to_ms_since_boot(get_absolute_time());
        if (!haveNtp || nowMs - lastNtpMs >= NTP_EVERY_MS) {
            time_t epoch;
            if (ntpOnce(epoch)) {
                net.ntpEpoch = epoch;
                net.ntpAtMs = to_ms_since_boot(get_absolute_time());
                net.ntpSeq = net.ntpSeq + 1;
                net.ntpOk = net.ntpOk + 1;
                haveNtp = true;
                lastNtpMs = nowMs;
            } else {
                net.ntpFail = net.ntpFail + 1;
                lastNtpMs = nowMs - NTP_EVERY_MS + 30000;  // try again in 30 s
                haveNtp = true;
            }
        }
        netSleep(1000);
    }
}

static const char *netText() {
    static char text[96];
    switch (net.state) {
    case Net::RADIO_STARTING:
        snprintf(text, sizeof(text), "radio starting (try %d)", net.radioTries);
        break;
    case Net::NO_RM2:
        snprintf(text, sizeof(text), "NO RM2 after %d tries (cyw43 %d)", net.radioTries, net.radioError);
        break;
    case Net::NO_CREDS:
        snprintf(text, sizeof(text), "radio up, no WiFi set (press w)");
        break;
    case Net::JOINING:
        snprintf(text, sizeof(text), "joining \"%s\"", creds.ssid);
        break;
    case Net::JOIN_FAILED:
        snprintf(text, sizeof(text), "JOIN FAILED \"%s\" (err %d), retrying", creds.ssid, net.lastJoinError);
        break;
    case Net::UP:
        snprintf(text, sizeof(text), "WiFi %s %ld dBm", net.ip, static_cast<long>(net.rssi));
        break;
    }
    return text;
}

// ---------------------------------------------------------------------------------------------
// LEDs: frames in PSRAM, two SRAM buffers on the wire.

#define STRANDS 32
#define PIXELS 200
#define FRAMES 100
#define FRAME_MS 50
#define FRAME_WORDS (PIXELS * 24 * 3)

static uint32_t __uninitialized_psram("uber_frames") psramFrames[FRAMES][FRAME_WORDS];
#define STRESS_BYTES (2 * 1024 * 1024)
static uint32_t __uninitialized_psram("uber_stress") psramStress[STRESS_BYTES / 4];

static uint32_t wire[2][FRAME_WORDS];
static uint32_t frameHash[FRAMES];

static uint32_t hashWords(const uint32_t *w, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

static inline uintptr_t uncached(const void *p) {
    return reinterpret_cast<uintptr_t>(p) - kCachedBase + kUncachedBase;
}

static uint32_t pixelRgb(int strand, int px, int frame) {
    // OUTk = GPIO 8(k-1) .. 8k-1.
    static const float colors[4][3] = {{1.0f, 0.55f, 0.08f}, {1.0f, 0.04f, 0.02f}, {0.05f, 1.0f, 0.1f}, {1.0f, 0.75f, 0.45f}};
    const float *c = colors[strand / 8];
    float phase = px / 40.0f - frame / static_cast<float>(FRAMES) + strand / 32.0f;
    float wave = 0.5f + 0.5f * sinf(6.2831853f * phase);
    float level = (0.12f + 0.88f * wave * wave) * UBER_BRIGHTNESS;
    uint32_t r = static_cast<uint32_t>(c[0] * level + 0.5f);
    uint32_t g = static_cast<uint32_t>(c[1] * level + 0.5f);
    uint32_t b = static_cast<uint32_t>(c[2] * level + 0.5f);
    return r << 16 | g << 8 | b;  // seed pixels take R, G, B
}

static void buildFrame(int f, uint32_t *out) {
    size_t w = 0;
    for (int i = 0; i < PIXELS; i++) {
        uint32_t rgb[STRANDS];
        for (int s = 0; s < STRANDS; s++) rgb[s] = pixelRgb(s, i, f);
        for (int bit = 23; bit >= 0; bit--) {
            uint32_t data = 0;
            for (int s = 0; s < STRANDS; s++) data |= ((rgb[s] >> bit) & 1u) << s;
            out[w++] = 0xffffffffu;
            out[w++] = data;
            out[w++] = 0;
        }
    }
}

static int copyDma = -1;
static int wireDma;
static PIO wirePio = pio0;
static uint wireSm;

static void dmaCopyBlocking(void *dst, const void *src, uint32_t bytes) {
    dma_channel_config c = dma_channel_get_default_config(copyDma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(copyDma, &c, dst, src, bytes / 4, true);
    dma_channel_wait_for_finish_blocking(copyDma);
}

static void buildAllFrames() {
    uint64_t t = time_us_64();
    for (int f = 0; f < FRAMES; f++) {
        buildFrame(f, wire[0]);
        frameHash[f] = hashWords(wire[0], FRAME_WORDS);
        dmaCopyBlocking(reinterpret_cast<void *>(uncached(psramFrames[f])), wire[0], sizeof(wire[0]));
    }
    printf("  LEDs: %d frames (%u KB) built into PSRAM in %.2f s\n", FRAMES,
           static_cast<unsigned>(sizeof(psramFrames) / 1024), (time_us_64() - t) / 1e6);
}

static void wireSetup() {
    // pio0 for GPIO 0-31. The cyw43 bus (GPIO 43-46) needs a PIO with its GPIO base at 16, so it
    // takes pio1 or pio2 and can't collide with this one.
    wireSm = pio_claim_unused_sm(wirePio, true);
    uint offset = pio_add_program(wirePio, &ws2811_parallel_program);
    ws2811_parallel_program_init(wirePio, wireSm, offset, 0, STRANDS, 800000);
    wireDma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(wireDma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(wirePio, wireSm, true));
    dma_channel_configure(wireDma, &c, &wirePio->txf[wireSm], wire[0], FRAME_WORDS, false);
}

// ---------------------------------------------------------------------------------------------
// Core 0 bookkeeping.

struct Stats {
    uint32_t frames = 0;
    uint32_t badFrames = 0;
    uint32_t lateFrames = 0;
    uint32_t copyUsMax = 0;
    uint64_t copyUsSum = 0;
    uint64_t stressBytes = 0;
    uint32_t stressBad = 0;
    uint32_t stressSweeps = 0;
    uint32_t rtcWrites = 0;
    long lastRtcOffByS = 0;
    bool rtcSetOnce = false;
};
static Stats stats;

static float dieTemperatureC() {
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    float volts = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (volts - 0.706f) / 0.001721f;
}

static void handleNtp(const Ds3231 &rtc, uint32_t &seenSeq) {
    if (net.ntpSeq == seenSeq) return;
    seenSeq = net.ntpSeq;
    // The answer may be up to a second old by now; correct for that.
    time_t epoch = static_cast<time_t>(net.ntpEpoch) +
                   (to_ms_since_boot(get_absolute_time()) - net.ntpAtMs + 500) / 1000;
    struct tm utc;
    gmtime_r(&epoch, &utc);
    datetime_t dt;
    tm_to_datetime(&utc, &dt);
    long off = 0;
    bool known = false;
    if (auto before = rtc.getDatetime()) {
        off = static_cast<long>(datetimeToEpoch(*before) - epoch);
        known = true;
    }
    if (rtc.setDatetime(dt)) {
        stats.rtcWrites++;
        stats.lastRtcOffByS = off;
        stats.rtcSetOnce = true;
        if (known) {
            printf("  NTP: RTC was off by %ld s, set it\n", off);
        } else {
            printf("  NTP: RTC set\n");
        }
    } else {
        printf("  NTP: got the time but the RTC write FAILED\n");
    }
}

static void printStatus(const Ds3231 &rtc, uint32_t seconds, bool havePsram) {
    char when[40] = "RTC NOT ANSWERING";
    if (auto now = rtc.getDatetime()) formatRtcLocal(*now, when, sizeof(when));
    printf(">>> %s | %s | ", when, netText());
    if (havePsram) {
        uint32_t avg = stats.frames ? static_cast<uint32_t>(stats.copyUsSum / stats.frames) : 0;
        printf("PSRAM %s: %lu MB stressed, %lu frames (%lu bad), copy %lu us | ",
               (stats.stressBad || stats.badFrames) ? "*** BAD ***" : "ok",
               static_cast<unsigned long>(stats.stressBytes >> 20), static_cast<unsigned long>(stats.frames),
               static_cast<unsigned long>(stats.badFrames), static_cast<unsigned long>(avg));
    } else {
        printf("NO PSRAM | ");
    }
    printf("die %.1f C  [%lus]\n", dieTemperatureC(), static_cast<unsigned long>(seconds));
}

static void printReport(const Ds3231 &rtc, bool havePsram) {
    printf("\n==== uber_test: LED processor board soak ====\n");
    printf("Clock:  clk_sys %.1f MHz\n", frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000.0f);
    printf("Radio:  %s, %d tr%s to come up\n", netText(), net.radioTries, net.radioTries == 1 ? "y" : "ies");
    printf("WiFi:   network \"%s\"%s, joins %lu, drops %lu\n", creds.ssid[0] ? creds.ssid : "(none)",
           strlen(WIFI_SSID) ? " (built in)" : "", static_cast<unsigned long>(net.joins),
           static_cast<unsigned long>(net.drops));
    printf("NTP:    %lu ok, %lu failed", static_cast<unsigned long>(net.ntpOk), static_cast<unsigned long>(net.ntpFail));
    if (net.ntpOk) {
        printf(", last %lu s ago", static_cast<unsigned long>((to_ms_since_boot(get_absolute_time()) - net.ntpAtMs) / 1000));
    }
    printf("\n");
    if (auto now = rtc.getDatetime()) {
        char text[40];
        formatRtcLocal(*now, text, sizeof(text));
        printf("RTC:    %s (%04d-%02d-%02d %02d:%02d:%02d UTC)", text, now->year, now->month, now->day,
               now->hour, now->min, now->sec);
        if (auto t = rtc.getTemperatureC()) printf(", %.2f C", *t);
        if (stats.rtcSetOnce) printf(", set %lu times from NTP, last off by %ld s", static_cast<unsigned long>(stats.rtcWrites), stats.lastRtcOffByS);
        printf("\n");
    } else {
        printf("RTC:    DS3231 NOT answering\n");
    }
    if (havePsram) {
        printf("PSRAM:  %u MB; frames %lu played, %lu bad, %lu late; copy avg %lu us max %lu us; "
               "stress %lu MB in %lu sweeps, %lu bad words\n",
               static_cast<unsigned>(size() >> 20), static_cast<unsigned long>(stats.frames),
               static_cast<unsigned long>(stats.badFrames), static_cast<unsigned long>(stats.lateFrames),
               static_cast<unsigned long>(stats.frames ? stats.copyUsSum / stats.frames : 0),
               static_cast<unsigned long>(stats.copyUsMax), static_cast<unsigned long>(stats.stressBytes >> 20),
               static_cast<unsigned long>(stats.stressSweeps), static_cast<unsigned long>(stats.stressBad));
        printLog(8);
    } else {
        printf("PSRAM:  NOT FOUND (U4 didn't answer its ID read), LEDs off\n");
    }
    printf("Keys:   w = set WiFi\n");
    printf("=============================================\n");
}

int main() {
    bool havePsram = present();

    stdio_init_all();
    gpio_init(HEARTBEAT_PIN);
    gpio_set_dir(HEARTBEAT_PIN, GPIO_OUT);
    adc_init();
    adc_set_temp_sensor_enabled(true);
    rtcLocalTimeInit();
    rtcBusInit();
    Ds3231 rtc(RTC_I2C);

    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);
    printf("\n==== uber_test starting ====\n");

    // Scratch registers survive a watchdog reboot (how w asks for setup) but not a power-on.
    bool setupAsked = watchdog_hw->scratch[0] == SETUP_SCRATCH_MAGIC;
    watchdog_hw->scratch[0] = 0;
    bool haveCreds = loadCreds();
    if (setupAsked) {
        askForCreds(0);
    } else if (!haveCreds) {
        askForCreds(60000);
    } else {
        printf("  WiFi: \"%s\"%s (press w any time to change)\n", creds.ssid, strlen(WIFI_SSID) ? " (built in)" : "");
    }

    copyDma = dma_claim_unused_channel(true);

    // Frames go into the PSRAM before core 1 starts: nothing else is on the QMI bus yet, and a
    // missing chip is known before anything depends on it.
    if (havePsram && UBER_LEDS) buildAllFrames();
    if (havePsram && UBER_LEDS) wireSetup();

    multicore_launch_core1(core1Main);

    uint32_t stressOff = havePsram ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(psramStress) - kCachedBase) : 0;
    uint32_t stressPos = 0;
    uint32_t seenNtp = 0;
    int cur = 0;
    int frame = 0;

    if (havePsram && UBER_LEDS) {
        dmaCopyBlocking(wire[0], reinterpret_cast<const void *>(uncached(psramFrames[0])), sizeof(wire[0]));
    }

    absolute_time_t next = get_absolute_time();
    absolute_time_t nextSecond = next;
    uint32_t seconds = 0;
    while (true) {
        if (havePsram && UBER_LEDS) {
            dma_channel_set_read_addr(wireDma, wire[cur], true);

            // Next frame: PSRAM -> the other SRAM buffer, then check it.
            frame = (frame + 1) % FRAMES;
            uint64_t t = time_us_64();
            dmaCopyBlocking(wire[cur ^ 1], reinterpret_cast<const void *>(uncached(psramFrames[frame])), sizeof(wire[0]));
            uint32_t us = static_cast<uint32_t>(time_us_64() - t);
            stats.copyUsSum += us;
            if (us > stats.copyUsMax) stats.copyUsMax = us;
            stats.frames++;
            if (hashWords(wire[cur ^ 1], FRAME_WORDS) != frameHash[frame]) {
                stats.badFrames++;
                printf("  *** frame %d came back from PSRAM wrong\n", frame);
            }
        }
        if (havePsram) {
            // 32 KB of the stress region per frame, alternating plain and through-the-cache.
            constexpr uint32_t kChunk = 32 * 1024;
            uint32_t seed = stats.stressSweeps * 0x01000193u + 7;
            bool cached = (stressPos / kChunk) & 1;
            stats.stressBad += fillVerify(stressOff + stressPos, kChunk, seed, cached, !cached);
            stats.stressBytes += kChunk;
            stressPos += kChunk;
            if (stressPos >= STRESS_BYTES) {
                stressPos = 0;
                stats.stressSweeps++;
            }
        }

        if (time_reached(nextSecond)) {
            nextSecond = delayed_by_ms(nextSecond, 1000);
            seconds++;
            gpio_put(HEARTBEAT_PIN, seconds & 1);
            handleNtp(rtc, seenNtp);
            if (seconds % 30 == 1) printReport(rtc, havePsram);
            printStatus(rtc, seconds, havePsram);
        }

        int c = getchar_timeout_us(0);
        if (c == 'w' || c == 'W') {
            printf("\n>>> rebooting to set WiFi...\n");
            sleep_ms(100);
            watchdog_hw->scratch[0] = SETUP_SCRATCH_MAGIC;
            watchdog_reboot(0, 0, 10);
            while (true) tight_loop_contents();
        }

        if (havePsram && UBER_LEDS) {
            dma_channel_wait_for_finish_blocking(wireDma);
            cur ^= 1;
        }
        next = delayed_by_ms(next, FRAME_MS);
        if (time_reached(next)) {
            stats.lateFrames++;
            next = get_absolute_time();
        } else {
            sleep_until(next);
        }
    }
}
