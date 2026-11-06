// Over-the-air firmware updates for the processor board, using the RP2350 bootrom's A/B partitions
// and "try before you buy" (TBYB). See firmware/ota/README.md for the flash layout and the steps.
//
// The flow, from the board's side:
//
//   1. Boot. ota::bootState() says which slot (A or B) we're running from and whether this is a
//      trial boot of a freshly pushed image (the bootrom armed a 16.7 s watchdog for it).
//   2. Join WiFi and start an ota::Server. Only then call ota::commit(): an image that can't take the
//      next update must never become the "good" one. If commit() isn't called in time, the watchdog
//      fires and the bootrom goes back to the previous image.
//   3. tools/ota_push.py on the laptop connects to TCP port 4242 and streams the new .uf2's flash
//      blocks. The server writes them into the slot we're NOT running from, checks every block
//      against flash and a SHA-256 of the whole stream, then asks for a "flash update" reboot into
//      the new slot, which boots it as a trial (step 1 again).
//
// Network calls are lwIP raw API and must run in the lwIP context: from the main loop in
// pico_cyw43_arch_lwip_poll builds (call Server::poll() and cyw43_arch_poll() from the same loop),
// or between cyw43_arch_lwip_begin()/end() otherwise. Flash writes go through flash_safe_execute(),
// so if the other core is running from flash it must have called multicore_lockout_victim_init().
#pragma once

#include <cstddef>
#include <cstdint>

struct tcp_pcb;
struct pbuf;

namespace ota {

constexpr uint16_t kDefaultPort = 4242;

struct BootState {
    int partition = -1;        // partition index we booted from (0 = A, 1 = B), -1 = no partition table
    bool trial = false;        // TBYB image on its trial boot: call commit() or it rolls back
    bool flashUpdate = false;  // this boot was a "flash update" boot (after a USB load or an OTA push)
};

BootState bootState();

// "A", "B" or "?" for a partition index from BootState.
const char *slotName(int partition);

// Makes the running image the good one (rom_explicit_buy) and stops the TBYB watchdog. Does nothing
// and returns true if this isn't a trial boot. Returns false if the bootrom refused.
bool commit();

// Called with progress while an update streams in (blocksDone of blocksTotal).
using ProgressFn = void (*)(uint32_t blocksDone, uint32_t blocksTotal);

class Server {
public:
    // hello is sent to every client on connect, as the first line (no newline needed), e.g.
    // "fw=ota_test label=slow". The server adds the slot and trial/committed state to it.
    explicit Server(const char *hello, uint16_t port = kDefaultPort);

    bool start();

    // Refuse updates (clients still get the hello line). Used while a trial image is undecided.
    void setUpdatesAllowed(bool allowed) { updatesAllowed_ = allowed; }

    void setProgress(ProgressFn fn) { progress_ = fn; }

    // True once a complete, verified image has been written and the client told. The caller should
    // shut the radio down and call rebootIntoUpdate(), after a short delay so the reply gets out.
    bool updateReady() const { return updateReady_; }
    uint32_t updateReadyAtMs() const { return updateReadyAtMs_; }

    [[noreturn]] void rebootIntoUpdate();

    // Number of failed or aborted pushes since boot, and the reason for the last one.
    uint32_t failures() const { return failures_; }
    const char *lastError() const { return lastError_; }

private:
    enum class Rx { Header, Blocks, Done };

    static int8_t onAccept(void *arg, tcp_pcb *pcb, int8_t err);
    static int8_t onRecv(void *arg, tcp_pcb *pcb, pbuf *p, int8_t err);
    static int8_t onPoll(void *arg, tcp_pcb *pcb);
    static void onErr(void *arg, int8_t err);

    void sendLine(const char *text);
    // Returns true if the pcb had to be aborted (an lwIP callback must then return ERR_ABRT).
    bool closeClient(bool abort);
    void fail(const char *why);
    void consume(const uint8_t *data, size_t len);
    bool handleHeader();
    bool handleBlock();
    bool findTarget(uint32_t family);
    bool eraseThrough(uint32_t sectorIndex);

    const char *hello_;
    uint16_t port_;
    tcp_pcb *listener_ = nullptr;
    tcp_pcb *client_ = nullptr;
    bool updatesAllowed_ = true;
    ProgressFn progress_ = nullptr;

    Rx rx_ = Rx::Header;
    uint8_t record_[260];
    size_t recordLen_ = 0;
    uint32_t idlePolls_ = 0;

    uint32_t blocksTotal_ = 0;
    uint32_t blocksDone_ = 0;
    uint8_t expectedSha_[32];
    uint32_t lastAddr_ = 0;
    int32_t erasedThrough_ = -1;    // highest sector (relative to the partition) erased so far
    uint32_t targetStart_ = 0;      // flash offset of the slot being written
    uint32_t targetSize_ = 0;
    int targetPartition_ = -1;
    bool aborted_ = false;
    bool touchedTarget_ = false;

    bool updateReady_ = false;
    uint32_t updateReadyAtMs_ = 0;
    uint32_t failures_ = 0;
    char lastError_[48] = "";
};

}  // namespace ota
