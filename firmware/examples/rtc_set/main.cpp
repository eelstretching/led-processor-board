// Sets the DS3231 from the laptop over USB serial, then prints its time once a second (UTC, with
// US Eastern alongside) so you can see it ticking (and, after a power cycle on the coin cell, that
// it kept time).
//
// The RTC keeps UTC. Send it one line, either of:
//
//   1792889400              Unix time (seconds since 1970, UTC), e.g. from `date -u +%s`
//   2026-10-25 00:50:00     a UTC date and time
//
// tools/set_rtc.py does this for you from a Mac, to the second.
//
// If the chip says it lost power (new module, no or dead coin cell), this seeds it from the
// firmware's build time (as UTC) so it at least ticks from roughly now. If the chip doesn't answer, it
// scans the bus once and prints what it did find, which is the useful thing to know on a
// perfboard.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "pico/stdlib.h"
#include "pico/util/datetime.h"

#include "ds3231.hpp"
#include "rtc_pins.h"

// BUILD_EPOCH is the Unix time of the CMake configure (always UTC, whatever the build machine's
// time zone; __DATE__/__TIME__ would be its local time).
static datetime_t seedDatetimeFromBuild() {
    time_t t = static_cast<time_t>(BUILD_EPOCH);
    struct tm utc;
    gmtime_r(&t, &utc);
    datetime_t dt{};
    tm_to_datetime(&utc, &dt);
    return dt;
}

// Local time for the printout only; the RTC itself stays on UTC. POSIX rules for US Eastern:
// EDT from 2 am on the second Sunday in March to 2 am on the first Sunday in November.
#ifndef LOCAL_TZ
#define LOCAL_TZ "EST5EDT,M3.2.0,M11.1.0"
#endif

// Unix time of a UTC datetime_t (newlib has no timegm). Days-from-civil, after Howard Hinnant.
static time_t datetimeToEpoch(const datetime_t &dt) {
    int y = dt.year - (dt.month <= 2 ? 1 : 0);
    int era = y / 400;
    int yoe = y - era * 400;
    int mp = (dt.month + 9) % 12;
    int doy = (153 * mp + 2) / 5 + dt.day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = static_cast<long long>(era) * 146097 + doe - 719468;
    return static_cast<time_t>(days * 86400 + dt.hour * 3600 + dt.min * 60 + dt.sec);
}

static void printDatetime(const char *prefix, const datetime_t &dt) {
    printf("%s%04d-%02d-%02d %02d:%02d:%02d UTC", prefix, dt.year, dt.month, dt.day, dt.hour,
           dt.min, dt.sec);
    time_t t = datetimeToEpoch(dt);
    struct tm local;
    localtime_r(&t, &local);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);
    printf(" (%s)", text);
}

// Lists every address that ACKs, so a missing RTC can be told apart from a dead bus.
static void scanBus() {
    printf("I2C%d scan (SDA GP%d, SCL GP%d):", RTC_I2C_INDEX, RTC_SDA_PIN, RTC_SCL_PIN);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        if (i2c_read_timeout_us(RTC_I2C, addr, &dummy, 1, false, 2000) >= 0) {
            printf(" 0x%02x", addr);
            found++;
        }
    }
    // 0x68 is the DS3231; the usual modules also carry an AT24C32 EEPROM at 0x57.
    printf(found ? "\n" : " nothing (check wiring, power and pull-ups)\n");
}

// Parses a Unix time or a "YYYY-MM-DD HH:MM:SS" UTC time. An optional leading T is ignored.
static bool parseTime(const char *line, datetime_t &out) {
    while (*line == ' ' || *line == 'T' || *line == 't') {
        line++;
    }
    int y, mo, d, h, mi, s;
    if (std::sscanf(line, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
        if (y < 2000 || y > 2199 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
            mi < 0 || mi > 59 || s < 0 || s > 59) {
            return false;
        }
        out = {};
        out.year = static_cast<int16_t>(y);
        out.month = static_cast<int8_t>(mo);
        out.day = static_cast<int8_t>(d);
        out.hour = static_cast<int8_t>(h);
        out.min = static_cast<int8_t>(mi);
        out.sec = static_cast<int8_t>(s);
        return true;
    }
    char *end;
    long long epoch = std::strtoll(line, &end, 10);
    while (*end == ' ') {
        end++;
    }
    if (end != line && *end == '\0' && epoch >= 946684800LL) {
        time_t t = static_cast<time_t>(epoch);
        struct tm utc;
        gmtime_r(&t, &utc);
        tm_to_datetime(&utc, &out);
        return true;
    }
    return false;
}

int main() {
    stdio_init_all();
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    rtcBusInit();
    Ds3231 rtc(RTC_I2C);

    char line[48];
    size_t lineLen = 0;
    bool wasUp = false;
    absolute_time_t nextPrint = get_absolute_time();

    while (true) {
        // Collect a line from USB serial without blocking the once-a-second print. Anything that
        // isn't printable is dropped, so stray bytes can't hide in front of the time.
        int c = getchar_timeout_us(10 * 1000);
        if (c == '\r' || c == '\n') {
            line[lineLen] = '\0';
            if (lineLen > 0) {
                datetime_t dt;
                if (!parseTime(line, dt)) {
                    printf("Didn't understand \"%s\". Send Unix seconds or YYYY-MM-DD HH:MM:SS (UTC).\n",
                           line);
                } else if (!rtc.setDatetime(dt)) {
                    printf("Couldn't write the RTC\n");
                } else {
                    printDatetime("RTC set to ", dt);
                    printf("\n");
                }
            }
            lineLen = 0;
        } else if (c >= ' ' && c <= '~' && lineLen < sizeof(line) - 1) {
            line[lineLen++] = static_cast<char>(c);
        }

        if (!time_reached(nextPrint)) {
            continue;
        }
        nextPrint = delayed_by_ms(nextPrint, 1000);

        if (!rtc.init()) {
            // Say so (and scan) every few seconds rather than every second, so it stays readable.
            static int misses = 0;
            if (misses++ % 5 == 0) {
                printf("DS3231 not responding on I2C%d (SDA GP%d, SCL GP%d)\n", RTC_I2C_INDEX,
                       RTC_SDA_PIN, RTC_SCL_PIN);
                scanBus();
            }
            wasUp = false;
            continue;
        }
        if (!wasUp) {
            printf("DS3231 found on I2C%d (SDA GP%d, SCL GP%d)\n", RTC_I2C_INDEX, RTC_SDA_PIN,
                   RTC_SCL_PIN);
            if (rtc.lostPower()) {
                printf("It lost power since it was last set, so seeding it from this firmware's "
                       "build time. Send it the real time to fix that.\n");
                if (!rtc.setDatetime(seedDatetimeFromBuild())) {
                    printf("Couldn't seed the RTC\n");
                }
            }
            wasUp = true;
        }

        if (auto now = rtc.getDatetime()) {
            printDatetime("", *now);
            if (auto temp = rtc.getTemperatureC()) {
                printf("  %.2f C", *temp);
            }
            printf("\n");
        } else {
            printf("Couldn't read the RTC\n");
        }
    }
}
