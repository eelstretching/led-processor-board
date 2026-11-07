#include "ota.hpp"

#include <cstdio>
#include <cstring>

#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/sha256.h"
#include "pico/stdlib.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

namespace ota {

namespace {

// Wire format, all little-endian. After the board's hello line, the client sends one header:
//   u32 magic 'OTA1' (0x3141544f), u32 UF2 family id, u32 block count, u32 reserved (0),
//   u8[32] SHA-256 of all the block payloads, in order
// then `count` records of u32 flash address (0x10000000-based, as in the .uf2) + 256 data bytes,
// in strictly increasing address order. The board answers with one line: "OK <slot>" or
// "ERR <reason>".
constexpr uint32_t kMagic = 0x3141544f;
constexpr size_t kHeaderLen = 48;
constexpr size_t kRecordLen = 4 + 256;
constexpr uint32_t kIdleTimeoutPolls = 5;  // tcp_poll every 2 s -> drop a stalled push after 10 s

// rom_* partition calls need a word-aligned scratch area (3.25 KB today); buy needs 4 KB.
alignas(4) uint8_t workarea[4096];

pico_sha256_state_t sha;
bool shaRunning = false;

uint32_t le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct FlashOp {
    uint32_t offset;
    const uint8_t *data;  // nullptr = erase one sector
};

void __no_inline_not_in_flash_func(doFlashOp)(void *arg) {
    auto *op = static_cast<FlashOp *>(arg);
    if (op->data) {
        flash_range_program(op->offset, op->data, FLASH_PAGE_SIZE);
    } else {
        flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    }
}

bool flashOp(uint32_t offset, const uint8_t *data) {
    FlashOp op{offset, data};
    return flash_safe_execute(doFlashOp, &op, 1000) == PICO_OK;
}

// Flash contents at a physical offset, bypassing the cache and the address translation that maps
// the running slot to 0x10000000.
const uint8_t *flashAt(uint32_t offset) {
    return reinterpret_cast<const uint8_t *>(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset);
}

}  // namespace

BootState bootState() {
    BootState s;
    boot_info_t info{};
    if (rom_get_boot_info(&info)) {
        s.partition = info.partition;
        s.trial = info.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING;
    }
    s.flashUpdate = rom_get_last_boot_type() == BOOT_TYPE_FLASH_UPDATE;
    return s;
}

const char *slotName(int partition) {
    switch (partition) {
    case 0: return "A";
    case 1: return "B";
    default: return "?";
    }
}

bool commit() {
    if (!bootState().trial) return true;
    int rc = rom_explicit_buy(workarea, sizeof(workarea));
    if (rc) printf("ota: rom_explicit_buy failed (%d)\n", rc);
    return rc == 0 && !bootState().trial;
}

Server::Server(const char *hello, uint16_t port) : hello_(hello), port_(port) {}

bool Server::start() {
    tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;
    if (tcp_bind(pcb, nullptr, port_) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }
    listener_ = tcp_listen_with_backlog(pcb, 1);
    if (!listener_) {
        tcp_close(pcb);
        return false;
    }
    tcp_arg(listener_, this);
    tcp_accept(listener_, onAccept);
    return true;
}

err_t Server::onAccept(void *arg, tcp_pcb *pcb, err_t err) {
    auto *self = static_cast<Server *>(arg);
    if (err != ERR_OK || !pcb) return ERR_VAL;
    if (self->client_ || self->updateReady_) {
        // One push at a time, and none once we're about to reboot.
        tcp_write(pcb, "BUSY\n", 5, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        tcp_close(pcb);
        return ERR_OK;
    }
    self->client_ = pcb;
    self->rx_ = Rx::Header;
    self->recordLen_ = 0;
    self->idlePolls_ = 0;
    self->touchedTarget_ = false;
    tcp_arg(pcb, self);
    tcp_recv(pcb, onRecv);
    tcp_poll(pcb, onPoll, 4);
    tcp_err(pcb, onErr);

    BootState bs = bootState();
    char line[160];
    snprintf(line, sizeof(line), "OTA1 %s slot=%s state=%s updates=%s", self->hello_,
             slotName(bs.partition), bs.trial ? "trial" : "committed",
             self->updatesAllowed_ ? "yes" : "no");
    self->sendLine(line);
    return ERR_OK;
}

err_t Server::onRecv(void *arg, tcp_pcb *pcb, pbuf *p, err_t err) {
    auto *self = static_cast<Server *>(arg);
    if (!p) {
        // Client closed. Fine after a status query or a finished push; otherwise a failed push.
        if (self->rx_ == Rx::Blocks || (self->rx_ == Rx::Header && self->recordLen_ > 0)) {
            self->fail("client closed mid-push");
        }
        return self->closeClient(false) ? ERR_ABRT : ERR_OK;
    }
    self->idlePolls_ = 0;
    self->aborted_ = false;
    for (pbuf *q = p; q && self->client_ == pcb; q = q->next) {
        self->consume(static_cast<const uint8_t *>(q->payload), q->len);
    }
    if (self->client_ == pcb) tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return self->aborted_ ? ERR_ABRT : ERR_OK;
}

err_t Server::onPoll(void *arg, tcp_pcb *pcb) {
    auto *self = static_cast<Server *>(arg);
    if (self->rx_ == Rx::Blocks && ++self->idlePolls_ >= kIdleTimeoutPolls) {
        self->fail("timed out mid-push");
        self->closeClient(true);
        return ERR_ABRT;
    }
    return ERR_OK;
}

void Server::onErr(void *arg, err_t) {
    auto *self = static_cast<Server *>(arg);
    // The pcb is already gone.
    self->client_ = nullptr;
    if (self->rx_ == Rx::Blocks) self->fail("connection lost mid-push");
}

void Server::sendLine(const char *text) {
    if (!client_) return;
    tcp_write(client_, text, strlen(text), TCP_WRITE_FLAG_COPY);
    tcp_write(client_, "\n", 1, TCP_WRITE_FLAG_COPY);
    tcp_output(client_);
}

bool Server::closeClient(bool abort) {
    if (!client_) return false;
    tcp_pcb *pcb = client_;
    client_ = nullptr;
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    tcp_err(pcb, nullptr);
    bool aborted = abort || tcp_close(pcb) != ERR_OK;
    if (aborted) tcp_abort(pcb);
    if (rx_ != Rx::Done) rx_ = Rx::Header;
    recordLen_ = 0;
    return aborted;
}

void Server::fail(const char *why) {
    failures_++;
    snprintf(lastError_, sizeof(lastError_), "%s", why);
    printf("ota: push failed: %s\n", why);
    if (shaRunning) {
        sha256_result_t junk;
        pico_sha256_finish(&sha, &junk);
        shaRunning = false;
    }
    // Don't leave half an image in the other slot. (The bootrom wouldn't boot it, since the block
    // loop is incomplete, but an erased first sector makes that certain.)
    if (touchedTarget_) {
        flashOp(targetStart_, nullptr);
        touchedTarget_ = false;
    }
    if (client_) {
        char line[64];
        snprintf(line, sizeof(line), "ERR %s", why);
        sendLine(line);
    }
    rx_ = Rx::Header;
}

void Server::consume(const uint8_t *data, size_t len) {
    while (len && client_ && rx_ != Rx::Done) {
        size_t want = (rx_ == Rx::Header ? kHeaderLen : kRecordLen) - recordLen_;
        size_t n = len < want ? len : want;
        memcpy(record_ + recordLen_, data, n);
        recordLen_ += n;
        data += n;
        len -= n;
        if (recordLen_ < (rx_ == Rx::Header ? kHeaderLen : kRecordLen)) break;
        recordLen_ = 0;
        bool ok = rx_ == Rx::Header ? handleHeader() : handleBlock();
        if (!ok) {
            aborted_ = closeClient(false);
            return;
        }
    }
}

bool Server::findTarget(uint32_t family) {
    // After a watchdog boot (e.g. a trial that rolled back) the bootrom may not have the partition
    // table loaded yet.
    int rc = rom_load_partition_table(workarea, sizeof(workarea), false);
    if (rc) {
        fail("no partition table");
        return false;
    }
    resident_partition_t part{};
    rc = rom_get_uf2_target_partition(workarea, sizeof(workarea), family, &part);
    if (rc < 0) {
        fail("no slot takes this family");
        return false;
    }
    if (rc == bootState().partition) {
        fail("target slot is the running one");
        return false;
    }
    uint32_t first = (part.permissions_and_location & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >>
                     PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last = (part.permissions_and_location & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >>
                    PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    targetStart_ = first * FLASH_SECTOR_SIZE;
    targetSize_ = (last + 1 - first) * FLASH_SECTOR_SIZE;
    targetPartition_ = rc;
    printf("ota: writing slot %s (partition %d, flash 0x%06lx, %lu KB)\n", slotName(rc), rc,
           (unsigned long)targetStart_, (unsigned long)(targetSize_ / 1024));
    return true;
}

bool Server::handleHeader() {
    if (!updatesAllowed_) {
        fail("updates are off (trial boot)");
        return false;
    }
    if (le32(record_) != kMagic) {
        fail("bad header");
        return false;
    }
    uint32_t family = le32(record_ + 4);
    blocksTotal_ = le32(record_ + 8);
    memcpy(expectedSha_, record_ + 16, sizeof(expectedSha_));
    if (!findTarget(family)) return false;
    if (blocksTotal_ == 0 || blocksTotal_ > targetSize_ / 256) {
        fail("image doesn't fit the slot");
        return false;
    }
    if (pico_sha256_try_start(&sha, SHA256_BIG_ENDIAN, false) != PICO_OK) {
        fail("sha256 busy");
        return false;
    }
    shaRunning = true;
    blocksDone_ = 0;
    lastAddr_ = 0;
    erasedThrough_ = -1;
    rx_ = Rx::Blocks;
    printf("ota: receiving %lu blocks (%lu KB)\n", (unsigned long)blocksTotal_,
           (unsigned long)(blocksTotal_ / 4));
    return true;
}

bool Server::eraseThrough(uint32_t sector) {
    while (erasedThrough_ < static_cast<int32_t>(sector)) {
        erasedThrough_++;
        touchedTarget_ = true;
        if (!flashOp(targetStart_ + erasedThrough_ * FLASH_SECTOR_SIZE, nullptr)) return false;
    }
    return true;
}

bool Server::handleBlock() {
    uint32_t addr = le32(record_);
    const uint8_t *data = record_ + 4;
    if (addr < XIP_BASE || (addr & 0xff) || (blocksDone_ && addr <= lastAddr_)) {
        fail("bad block address");
        return false;
    }
    uint32_t rel = addr - XIP_BASE;
    if (rel + 256 > targetSize_) {
        fail("block outside the slot");
        return false;
    }
    if (!eraseThrough(rel / FLASH_SECTOR_SIZE) || !flashOp(targetStart_ + rel, data)) {
        fail("flash write failed");
        return false;
    }
    if (memcmp(flashAt(targetStart_ + rel), data, 256) != 0) {
        fail("flash verify failed");
        return false;
    }
    pico_sha256_update_blocking(&sha, data, 256);
    lastAddr_ = addr;
    blocksDone_++;
    if (progress_ && (blocksDone_ % 64 == 0 || blocksDone_ == blocksTotal_)) {
        progress_(blocksDone_, blocksTotal_);
    }
    if (blocksDone_ < blocksTotal_) return true;

    sha256_result_t got;
    pico_sha256_finish(&sha, &got);
    shaRunning = false;
    if (memcmp(got.bytes, expectedSha_, sizeof(expectedSha_)) != 0) {
        fail("sha256 mismatch");
        return false;
    }
    char line[32];
    snprintf(line, sizeof(line), "OK %s", slotName(targetPartition_));
    sendLine(line);
    rx_ = Rx::Done;
    touchedTarget_ = false;
    updateReady_ = true;
    updateReadyAtMs_ = to_ms_since_boot(get_absolute_time());
    printf("ota: image verified, ready to reboot into it\n");
    return true;
}

void Server::rebootIntoUpdate() {
    // A "flash update" boot of the new slot: the bootrom tries it first, and if it's TBYB-flagged
    // gives it 16.7 s to call commit() before falling back.
    rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 100, XIP_BASE + targetStart_, 0);
    while (true) tight_loop_contents();
}

}  // namespace ota
