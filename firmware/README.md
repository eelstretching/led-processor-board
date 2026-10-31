# Firmware

Pico SDK firmware for the processor board, starting with the DS3231 RTC. The same code also builds
for the perfboard compute board (a Pico 2 W with an off-the-shelf DS3231 module), so the RTC can be
brought up before the real boards arrive.

## Where the RTC is

`drivers/ds3231/include/rtc_pins.h` picks the bus and pins from `PICO_BOARD`:

| Board | `PICO_BOARD` | I2C | SDA | SCL | INT |
|---|---|---|---|---|---|
| LED processor board (U6) | `led_processor_board` (default) | I2C0 | GPIO32 | GPIO33 | GPIO34 |
| Perfboard Pico 2 W + DS3231 module | `pico2_w` | I2C1 | GP26 (pin 31) | GP27 (pin 32) | not wired |

Any of them can be overridden at configure time, e.g. `-DRTC_I2C_INDEX=0 -DRTC_SDA_PIN=4
-DRTC_SCL_PIN=5`. A pin that isn't on the chosen I2C peripheral fails the build (the RP2350's I2C
pins are fixed by GPIO number mod 4).

The RTC keeps **UTC**; shows convert to local time themselves (as MegaTree's `NewYear` does).

## Drivers

- `drivers/ds3231`: the `Ds3231` class (get/set the time, "lost power" check, temperature), moved
  over from universal-strip-controller's firmware, plus `rtc_pins.h` with `rtcBusInit()`:

```cpp
#include "ds3231.hpp"
#include "rtc_pins.h"

rtcBusInit();
Ds3231 rtc(RTC_I2C);
if (rtc.init()) {
    if (auto now = rtc.getDatetime()) {
        // use *now (UTC)
    }
}
```

## Examples

- `examples/rtc_set`: set the RTC from the laptop over USB serial, then print its time once a
  second. Send it Unix seconds or `YYYY-MM-DD HH:MM:SS` (UTC), or let `tools/set_rtc.py` do it.
  If the chip lost power it seeds itself from the build time; if it doesn't answer it scans the
  bus and lists what does (0x68 is the DS3231, 0x57 the EEPROM most modules also carry).
- `examples/wifi_rtc_sync`: join WiFi, get the time over NTP, write it to the RTC. Put the
  credentials in `firmware/wifi_secrets.cmake` (gitignored), which works from VS Code too:

  ```cmake
  set(WIFI_SSID "myssid")
  set(WIFI_PASSWORD "mypassword")
  ```

  `WIFI_SSID`/`WIFI_PASSWORD` environment variables or `-D` on the cmake line also work. Re-run
  the CMake configure after changing them; the values end up in `build/CMakeCache.txt` (also
  gitignored) and in the .uf2, so don't share a .uf2 built with them.
- `examples/strand_test`: a pixel tester for cutting reels into strands. Lights the first 200
  pixels on GP2 steady green at brightness 16/256 (RGB byte order, as for the seed pixels) and
  resends the frame every 20 ms, so a strand can be plugged in hot. GP2 is ribbon data 1 from a
  Pico 2 W and J5 (OUT1) data 3 on the processor board. `-DSTRAND_PIN=`, `-DSTRAND_LEN=` and
  `-DSTRAND_BRIGHTNESS=` change it. GP3 (ribbon data 2 from a Pico 2 W, J5 data 4 on the processor
  board) counts pixels: it lights 190 green, then adds the last 10 in red one per second, holds
  for 5 s and repeats. Ten red pixels means a full 200; fewer means the strand is short.
  `-DSTRAND_COUNT_PIN=` and `-DSTRAND_COUNT_BASE=` change it.

- `examples/bringup`: first power-on test for the processor board, with or without the RM2. GP35
  (J2) toggles at 1 Hz, since D5 is just the +5V power LED and D1 is on the radio's WL_GPIO0; with
  an RM2 fitted, D1 blinks along with it. Over USB serial it prints a status line every second (die
  temperature, DS3231 time and temperature, radio state) and every 10 s a full report: chip version
  and package, measured clocks, flash JEDEC ID and size, and an I2C scan if the DS3231 doesn't
  answer. It never touches GPIO 0-31, so output boards can stay plugged in. Build it for
  `led_processor_board`; `-DBRINGUP_HEARTBEAT_PIN=` moves the 1 Hz pin.

## Setting the clock from a Mac

1. Hold BOOTSEL, plug the board in, drag `rtc_set.uf2` onto the `RP2350` drive.
2. From this repo: `python3 firmware/tools/set_rtc.py`

It waits for the top of a second, sends the time, and shows a few lines from the board. To just
watch it tick: `screen /dev/cu.usbmodem* 115200` (quit with Ctrl-A then K).

## Building

Needs pico-sdk 2.3.0 and the Arm toolchain.

```sh
cd firmware
PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w   # perfboard
PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B build-board -G Ninja                  # processor board
ninja -C build        # -> build/examples/rtc_set/rtc_set.uf2
```

`boards/led_processor_board.h` is the reference board's `rp2350b_rm2.h` (same radio, flash and
PSRAM pins) with this board's RTC pins and UART1 on GPIO36/37 (J2) as the default UART.
