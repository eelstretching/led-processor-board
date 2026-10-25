// Connects to WiFi, fetches the current time over NTP, and writes it to
// the DS3231. NTP request/response handling is adapted from the pico-sdk
// pico-examples project (pico_w/wifi/ntp_client/picow_ntp_client.c). That
// part stays C-style free functions/callbacks -- lwIP's callback
// registration expects plain function pointers, which a class member
// function isn't -- but RTC access goes through the Ds3231 class.
#include <cstdio>
#include <cstring>
#include <ctime>

#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "ds3231.hpp"
#include "rtc_pins.h"

constexpr char kNtpServer[] = "pool.ntp.org";
constexpr size_t kNtpMsgLen = 48;
constexpr uint16_t kNtpPort = 123;
constexpr uint32_t kNtpDelta = 2208988800u; // seconds between 1 Jan 1900 and 1 Jan 1970
constexpr uint32_t kNtpResendTimeMs = 10 * 1000;
constexpr uint32_t kNtpTimeoutMs = 30 * 1000;

struct NtpState {
    ip_addr_t ntpServerAddress;
    udp_pcb *pcb;
    async_at_time_worker_t resendWorker;
    Ds3231 *rtc;
    volatile bool done;
    volatile bool ok;
};

static void ntpResult(NtpState *state, bool ok, const time_t *result) {
    async_context_remove_at_time_worker(cyw43_arch_async_context(), &state->resendWorker);
    if (ok && result) {
        struct tm utc;
        gmtime_r(result, &utc);
        datetime_t dt;
        tm_to_datetime(&utc, &dt);
        if (state->rtc->setDatetime(dt)) {
            char text[32];
            datetime_to_str(text, sizeof(text), &dt);
            printf("NTP time %s (UTC) written to the RTC\n", text);
        } else {
            printf("got NTP time but failed to write it to the RTC\n");
            ok = false;
        }
    } else {
        printf("NTP request failed\n");
    }
    state->ok = ok;
    state->done = true;
}

static void ntpRequest(NtpState *state) {
    // cyw43_arch_lwip_begin/end are no-ops in poll mode, but are required
    // around lwIP calls made outside of an lwIP callback when using
    // threadsafe_background mode.
    cyw43_arch_lwip_begin();
    pbuf *p = pbuf_alloc(PBUF_TRANSPORT, kNtpMsgLen, PBUF_RAM);
    uint8_t *req = static_cast<uint8_t *>(p->payload);
    memset(req, 0, kNtpMsgLen);
    req[0] = 0x1b;
    udp_sendto(state->pcb, p, &state->ntpServerAddress, kNtpPort);
    pbuf_free(p);
    cyw43_arch_lwip_end();
}

static void ntpDnsFound(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    auto *state = static_cast<NtpState *>(arg);
    if (ipaddr) {
        state->ntpServerAddress = *ipaddr;
        printf("ntp address %s\n", ipaddr_ntoa(ipaddr));
        ntpRequest(state);
    } else {
        printf("ntp dns request failed\n");
        ntpResult(state, false, nullptr);
    }
}

static void ntpRecv(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, u16_t port) {
    auto *state = static_cast<NtpState *>(arg);
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    if (ip_addr_cmp(addr, &state->ntpServerAddress) && port == kNtpPort && p->tot_len == kNtpMsgLen &&
        mode == 0x4 && stratum != 0) {
        uint8_t secondsBuf[4] = {0};
        pbuf_copy_partial(p, secondsBuf, sizeof(secondsBuf), 40);
        uint32_t secondsSince1900 = (static_cast<uint32_t>(secondsBuf[0]) << 24) |
                                     (static_cast<uint32_t>(secondsBuf[1]) << 16) |
                                     (static_cast<uint32_t>(secondsBuf[2]) << 8) | secondsBuf[3];
        time_t epoch = static_cast<time_t>(secondsSince1900 - kNtpDelta);
        ntpResult(state, true, &epoch);
    } else {
        printf("invalid ntp response\n");
        ntpResult(state, false, nullptr);
    }
    pbuf_free(p);
}

static void resendWorkerFn(__unused async_context_t *context, async_at_time_worker_t *worker) {
    auto *state = static_cast<NtpState *>(worker->user_data);
    printf("ntp request timed out\n");
    ntpResult(state, false, nullptr);
}

// Fetches the current time over NTP once and writes it to the RTC. Returns
// once that either succeeds, fails, or times out.
static bool syncRtcFromNtp(Ds3231 &rtc) {
    NtpState state{};
    state.rtc = &rtc;
    state.pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!state.pcb) {
        printf("failed to create UDP PCB\n");
        return false;
    }
    udp_recv(state.pcb, ntpRecv, &state);
    state.resendWorker.do_work = resendWorkerFn;
    state.resendWorker.user_data = &state;

    hard_assert(async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &state.resendWorker,
                                                        kNtpResendTimeMs));

    int err = dns_gethostbyname(kNtpServer, &state.ntpServerAddress, ntpDnsFound, &state);
    if (err == ERR_OK) {
        ntpRequest(&state); // cached DNS result, request immediately
    } else if (err != ERR_INPROGRESS) {
        printf("dns request failed\n");
        ntpResult(&state, false, nullptr);
    }

    absolute_time_t deadline = make_timeout_time_ms(kNtpTimeoutMs);
    while (!state.done && !time_reached(deadline)) {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(100));
#else
        sleep_ms(100);
#endif
    }

    udp_remove(state.pcb);
    return state.done && state.ok;
}

int main() {
    stdio_init_all();

    rtcBusInit();
    Ds3231 rtc(RTC_I2C);
    if (!rtc.init()) {
        printf("DS3231 not responding on the bus\n");
        return 1;
    }

    if (cyw43_arch_init()) {
        printf("failed to initialise wifi hardware\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("connecting to %s...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect to wifi\n");
        return 1;
    }
    printf("connected\n");

    if (!syncRtcFromNtp(rtc)) {
        printf("could not sync RTC from NTP\n");
    }

    cyw43_arch_disable_sta_mode();

    // Demonstrate the RTC is now actually ticking with the synced time.
    while (true) {
        if (auto now = rtc.getDatetime()) {
            char text[32];
            datetime_to_str(text, sizeof(text), &*now);
            printf("%s\n", text);
        }
        sleep_ms(1000);
    }
}
