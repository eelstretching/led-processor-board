#include "ds3231.hpp"

#include <cstring>

// DS3231 has no address pins -- it's always at 0x68.
namespace {

constexpr uint8_t kI2cAddr = 0x68u;

constexpr uint8_t kRegSeconds = 0x00u;
constexpr uint8_t kRegStatus = 0x0Fu;
constexpr uint8_t kRegTempMsb = 0x11u;

constexpr uint8_t kStatusOsfBit = 0x80u;

inline uint8_t bcdToBin(uint8_t bcd) {
    return static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
}

inline uint8_t binToBcd(uint8_t bin) {
    return static_cast<uint8_t>(((bin / 10) << 4) | (bin % 10));
}

// Sakamoto's algorithm. Returns 0 = Sunday .. 6 = Saturday, matching
// pico/util/datetime.h's datetime_t::dotw convention.
int dayOfWeek(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

} // namespace

bool Ds3231::writeRegs(uint8_t reg, const uint8_t *data, size_t len) const {
    uint8_t buf[8];
    buf[0] = reg;
    std::memcpy(&buf[1], data, len);
    int ret = i2c_write_blocking(i2c_, kI2cAddr, buf, len + 1, false);
    return ret == static_cast<int>(len + 1);
}

bool Ds3231::readRegs(uint8_t reg, uint8_t *data, size_t len) const {
    int ret = i2c_write_blocking(i2c_, kI2cAddr, &reg, 1, true);
    if (ret != 1) {
        return false;
    }
    ret = i2c_read_blocking(i2c_, kI2cAddr, data, len, false);
    return ret == static_cast<int>(len);
}

bool Ds3231::init() const {
    uint8_t seconds;
    return readRegs(kRegSeconds, &seconds, 1);
}

std::optional<datetime_t> Ds3231::getDatetime() const {
    uint8_t buf[7];
    if (!readRegs(kRegSeconds, buf, sizeof(buf))) {
        return std::nullopt;
    }

    datetime_t out{};
    out.sec = static_cast<int8_t>(bcdToBin(buf[0] & 0x7Fu));
    out.min = static_cast<int8_t>(bcdToBin(buf[1] & 0x7Fu));

    if (buf[2] & 0x40u) {
        // 12-hour mode: bit 5 is AM/PM, bits 0-4 are the BCD hour.
        int hour = bcdToBin(buf[2] & 0x1Fu) % 12;
        if (buf[2] & 0x20u) {
            hour += 12;
        }
        out.hour = static_cast<int8_t>(hour);
    } else {
        out.hour = static_cast<int8_t>(bcdToBin(buf[2] & 0x3Fu));
    }

    out.day = static_cast<int8_t>(bcdToBin(buf[4] & 0x3Fu));

    bool century = (buf[5] & 0x80u) != 0;
    out.month = static_cast<int8_t>(bcdToBin(buf[5] & 0x1Fu));
    out.year = static_cast<int16_t>(bcdToBin(buf[6]) + (century ? 2100 : 2000));

    // We don't trust the chip's own day-of-week register (its 1-7 mapping
    // is whatever it was last told, not necessarily consistent with the
    // date), so derive it ourselves.
    out.dotw = static_cast<int8_t>(dayOfWeek(out.year, out.month, out.day));

    return out;
}

bool Ds3231::setDatetime(const datetime_t &dt) const {
    bool century = dt.year >= 2100;
    uint8_t yearInCentury = static_cast<uint8_t>(dt.year - (century ? 2100 : 2000));

    uint8_t buf[7] = {
        binToBcd(static_cast<uint8_t>(dt.sec)),
        binToBcd(static_cast<uint8_t>(dt.min)),
        binToBcd(static_cast<uint8_t>(dt.hour)), // bit 6 = 0 selects 24-hour mode
        binToBcd(static_cast<uint8_t>(dt.dotw + 1)), // chip wants 1-7, meaning is ours alone
        binToBcd(static_cast<uint8_t>(dt.day)),
        static_cast<uint8_t>(binToBcd(static_cast<uint8_t>(dt.month)) | (century ? 0x80u : 0u)),
        binToBcd(yearInCentury),
    };

    if (!writeRegs(kRegSeconds, buf, sizeof(buf))) {
        return false;
    }

    // Setting the time is the one point where we can vouch for it, so
    // clear the "oscillator stopped" flag.
    uint8_t status;
    if (!readRegs(kRegStatus, &status, 1)) {
        return false;
    }
    status &= static_cast<uint8_t>(~kStatusOsfBit);
    return writeRegs(kRegStatus, &status, 1);
}

bool Ds3231::lostPower() const {
    uint8_t status;
    if (!readRegs(kRegStatus, &status, 1)) {
        // If we can't even talk to the chip, its time certainly can't be
        // trusted -- report that as "lost power" rather than silently
        // returning false-i.e.-fine.
        return true;
    }
    return (status & kStatusOsfBit) != 0;
}

std::optional<float> Ds3231::getTemperatureC() const {
    uint8_t buf[2];
    if (!readRegs(kRegTempMsb, buf, sizeof(buf))) {
        return std::nullopt;
    }
    // The two registers together are a single 10-bit two's-complement value
    // in units of 0.25 degrees; combine them before scaling rather than
    // treating the whole-degree and fractional parts as separately signed.
    int16_t raw = static_cast<int16_t>(static_cast<int8_t>(buf[0]) * 4) | static_cast<int16_t>(buf[1] >> 6);
    return static_cast<float>(raw) * 0.25f;
}
