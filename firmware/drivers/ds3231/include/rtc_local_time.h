// Local-time helpers for printouts. The DS3231 itself always keeps UTC; this only formats it.
//
// Call rtcLocalTimeInit() once at startup (it sets newlib's TZ), then formatRtcLocal() as often as
// you like. Override the zone with -DLOCAL_TZ="..." (POSIX TZ syntax).
#ifndef RTC_LOCAL_TIME_H
#define RTC_LOCAL_TIME_H

#include <cstddef>
#include <cstdlib>
#include <ctime>

#include "pico/types.h"

// POSIX rules for US Eastern: EDT from 2 am on the second Sunday in March to 2 am on the first
// Sunday in November.
#ifndef LOCAL_TZ
#define LOCAL_TZ "EST5EDT,M3.2.0,M11.1.0"
#endif

static inline void rtcLocalTimeInit() {
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
}

// Unix time of a UTC datetime_t (newlib has no timegm). Days-from-civil, after Howard Hinnant.
static inline time_t datetimeToEpoch(const datetime_t &dt) {
    int y = dt.year - (dt.month <= 2 ? 1 : 0);
    int era = y / 400;
    int yoe = y - era * 400;
    int mp = (dt.month + 9) % 12;
    int doy = (153 * mp + 2) / 5 + dt.day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = static_cast<long long>(era) * 146097 + doe - 719468;
    return static_cast<time_t>(days * 86400 + dt.hour * 3600 + dt.min * 60 + dt.sec);
}

// Writes a UTC datetime_t as local time, e.g. "2026-11-06 07:41:00 EST".
static inline void formatRtcLocal(const datetime_t &utc, char *text, size_t size) {
    time_t t = datetimeToEpoch(utc);
    struct tm local;
    localtime_r(&t, &local);
    strftime(text, size, "%Y-%m-%d %H:%M:%S %Z", &local);
}

#endif // RTC_LOCAL_TIME_H
