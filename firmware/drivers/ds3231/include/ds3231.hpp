// DS3231 real-time clock driver.
//
// This is a task-level API, not a register map: callers ask for the time,
// set the time, or check whether the time is trustworthy, and never see an
// I2C transaction or a BCD byte.
#ifndef DS3231_HPP
#define DS3231_HPP

#include <optional>

#include "hardware/i2c.h"
#include "pico/util/datetime.h"

// One DS3231 on the bus. The chip has a fixed I2C address (0x68, no address
// pins), so an instance just needs the I2C peripheral it's wired to.
class Ds3231 {
public:
    // Binds to the I2C peripheral the DS3231 is on. The caller must have
    // already configured that peripheral (i2c_init(), SDA/SCL pin
    // functions, pull-ups) before calling init().
    explicit Ds3231(i2c_inst_t *i2c) : i2c_(i2c) {}

    // Confirms the chip answers on the bus. Call once before using the rest
    // of the API. Returns false if it doesn't respond.
    bool init() const;

    // Reads the current date and time, or nullopt on an I2C error.
    std::optional<datetime_t> getDatetime() const;

    // Sets the date and time, e.g. after a fresh battery install or a sync
    // against a trusted clock. Also clears the "lost power" flag.
    bool setDatetime(const datetime_t &dt) const;

    // True if the RTC can't vouch for its own time: oscillator has stopped
    // at some point since it was last set (dead/missing backup battery,
    // chip never set after first power-up, etc). Check this before
    // trusting getDatetime() after a power cycle.
    bool lostPower() const;

    // Chip's onboard temperature sensor, in degrees Celsius, or nullopt on
    // an I2C error. Updated by the chip roughly every 64 seconds; not a
    // substitute for a real ambient temperature sensor, but useful as a
    // rough die/enclosure reading.
    std::optional<float> getTemperatureC() const;

private:
    i2c_inst_t *i2c_;

    bool writeRegs(uint8_t reg, const uint8_t *data, size_t len) const;
    bool readRegs(uint8_t reg, uint8_t *data, size_t len) const;
};

#endif // DS3231_HPP
