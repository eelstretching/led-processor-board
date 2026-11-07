// First over-the-air update test for the processor board.
//
// Joins WiFi, listens for tools/ota_push.py on TCP port 4242 (and answers to <host>.local over
// mDNS), and blinks D1 (the RM2's LED) and J2's GP35 at a rate that's different in each build, so
// you can see which image is running from across the room. A status line goes out over USB serial
// every 5 s.
//
// The image is "try before you buy": after an OTA push (or a USB load) the bootrom runs it once
// with a 16.7 s watchdog, and this firmware only commits it once WiFi is up and the update server
// is listening, i.e. once it could take the next update. Built with OTA_SKIP_COMMIT=1 it never
// commits, which is how to watch the board roll itself back to the previous image.
//
// It never touches GPIO 0-31, so output boards can stay plugged in.
#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "lwip/apps/mdns.h"
#include "lwip/netif.h"

#include "ota.hpp"

#ifndef OTA_LABEL
#define OTA_LABEL "dev"
#endif
#ifndef BLINK_MS
#define BLINK_MS 500
#endif
#ifndef OTA_SKIP_COMMIT
#define OTA_SKIP_COMMIT 0
#endif
#ifndef HEARTBEAT_PIN
#define HEARTBEAT_PIN 35
#endif

// ota_push.py looks for this in the .uf2 to know which build it is sending, and checks the board
// reports the same label after the reboot.
static const char kLabelTag[] = "OTA_LABEL=" OTA_LABEL ";";
static const char *label() { return kLabelTag + 10; }  // "slow;" -> printed up to the ';'

static constexpr uint32_t kStatusEveryMs = 5000;
static constexpr uint32_t kRetryWifiMs = 10000;
static constexpr uint32_t kTbybWatchdogMs = 16777;  // 0xffffff us, armed by the bootrom at launch

static uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

static int labelLen() { return static_cast<int>(strchr(label(), ';') - label()); }

static void onProgress(uint32_t done, uint32_t total) {
    printf("ota: %lu/%lu blocks (%lu%%)\n", (unsigned long)done, (unsigned long)total,
           (unsigned long)(done * 100 / total));
}

static void connectWifi() {
    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
}

int main() {
    stdio_init_all();
    const ota::BootState boot = ota::bootState();

    gpio_init(HEARTBEAT_PIN);
    gpio_set_dir(HEARTBEAT_PIN, GPIO_OUT);

    char host[24];
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    snprintf(host, sizeof(host), "procboard-%02x%02x", id.id[6], id.id[7]);

    if (cyw43_arch_init()) {
        // No radio, no network, no commit: a trial boot of this rolls back on its own.
        while (true) {
            printf(">>> OTA TEST %.*s: radio didn't start\n", labelLen(), label());
            gpio_xor_mask64(1ull << HEARTBEAT_PIN);
            sleep_ms(100);
        }
    }
    cyw43_arch_enable_sta_mode();
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], host);
    // Power save off: much quicker replies, and the board isn't on a battery.
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    connectWifi();

    char hello[96];
    snprintf(hello, sizeof(hello), "fw=ota_test label=%.*s host=%s", labelLen(), label(), host);
    ota::Server server(hello);
    server.setProgress(onProgress);
    server.setUpdatesAllowed(!boot.trial);

    bool online = false;
    bool serverUp = false;
    bool committed = !boot.trial;
    bool led = false;
    uint32_t nextBlink = 0, nextStatus = 0, nextRetry = nowMs() + kRetryWifiMs;

    while (true) {
        cyw43_arch_poll();
        const uint32_t now = nowMs();
        const int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

        if (link == CYW43_LINK_UP && !serverUp) {
            serverUp = true;
            printf("WiFi up: %s (%s.local)\n", ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])),
                   host);
            mdns_resp_init();
            mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], host);
            bool listening = server.start();
            printf("update server %s on port %u\n", listening ? "listening" : "FAILED TO START",
                   ota::kDefaultPort);
            if (boot.trial && listening) {
                if (OTA_SKIP_COMMIT) {
                    printf(">>> NOT committing (OTA_SKIP_COMMIT build): rolling back soon\n");
                } else if (ota::commit()) {
                    committed = true;
                    server.setUpdatesAllowed(true);
                    printf(">>> COMMITTED: this image is now the one that boots\n");
                } else {
                    printf(">>> commit FAILED: rolling back soon\n");
                }
            }
        }
        online = link == CYW43_LINK_UP;
        // Joining (1) and waiting for DHCP (2) are in progress; anything else means try again. The
        // listener survives a dropped link, so a rejoin needs nothing more.
        if (!online && link != CYW43_LINK_JOIN && link != CYW43_LINK_NOIP && now >= nextRetry) {
            printf("WiFi link status %d, retrying\n", link);
            connectWifi();
            nextRetry = now + kRetryWifiMs;
        }

        if (now >= nextBlink) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            gpio_put(HEARTBEAT_PIN, led);
            nextBlink = now + BLINK_MS;
        }

        if (now >= nextStatus) {
            nextStatus = now + kStatusEveryMs;
            printf(">>> OTA TEST %.*s | slot %s | %s | %s %s | blink %d ms | pushes failed %lu%s%s\n",
                   labelLen(), label(), ota::slotName(boot.partition),
                   committed ? "committed" : "TRIAL", online ? "up" : "no wifi",
                   online ? ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])) : "",
                   BLINK_MS, (unsigned long)server.failures(), server.failures() ? ", last: " : "",
                   server.lastError());
            if (!committed) {
                int left = static_cast<int>(kTbybWatchdogMs) - static_cast<int>(now);
                printf("    not committed: the bootrom rolls back to the old image in ~%d s\n",
                       left > 0 ? left / 1000 : 0);
            }
        }

        if (server.updateReady() && now - server.updateReadyAtMs() > 500) {
            printf(">>> new image written, rebooting into it\n");
            sleep_ms(100);  // let USB drain
            cyw43_arch_deinit();
            server.rebootIntoUpdate();
        }

        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
    }
}
