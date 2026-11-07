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
  board) counts pixels: it lights 190 green, then adds the last 10 in blue one per second, holds
  for 5 s and repeats. Every 10th pixel (10, 20, ... 200) is a red tick mark, as in all_strands.
  Ten new pixels (nine blue and the red tick at 200) means a full 200; fewer means it's short.
  `-DSTRAND_COUNT_PIN=` and `-DSTRAND_COUNT_BASE=` change it.

- `examples/bringup`: first power-on test for the processor board, with or without the RM2. GP35
  (J2) toggles at 1 Hz, since D5 is just the +5V power LED and D1 is on the radio's WL_GPIO0; with
  an RM2 fitted, D1 blinks along with it. Over USB serial it prints a status line every second (die
  temperature, DS3231 time and temperature, radio state) and every 10 s a full report: chip version
  and package, measured clocks, flash JEDEC ID and size, and an I2C scan if the DS3231 doesn't
  answer. It never touches GPIO 0-31, so output boards can stay plugged in. Build it for
  `led_processor_board`; `-DBRINGUP_HEARTBEAT_PIN=` moves the 1 Hz pin.

- `examples/all_strands`: every output at once on the processor board. GPIO 0-31 (all four output
  shrouds, J5-J8) each show 200 green pixels with every 10th one red as a tick mark (10, 20, ... 200)
  at brightness 16/256, driven together by one PIO state machine and resent every 20 ms, so strands
  can be plugged in hot and unused outputs do no harm. A full strand ends on a red tick.
  `-DALL_STRANDS_LEN=`, `-DALL_STRANDS_TICK=` and `-DALL_STRANDS_BRIGHTNESS=` change it.
  Build it for `led_processor_board`.

- `examples/ota_test` + `drivers/ota` + `tools/ota_push.py`: over-the-air updates. The flash gets
  A/B firmware slots (`ota/partitions.json`), a new image is pushed over WiFi into the slot that
  isn't running, and the bootrom's try-before-you-buy rolls back anything that doesn't come up on
  WiFi within 16.7 s. Layout, one-time USB setup and the push/rollback/stress test are in
  `ota/README.md`. Build it for `led_processor_board`, with `wifi_secrets.cmake` filled in.

- `examples/pin_test`: level-shifter check for output boards. GPIO 0-31 (J5-J8) all go high for
  1 s, then low for 1 s, forever; put a meter on the output board's terminals and each should swing
  0 V / 5 V. D1 (RM2 LED) and J2's GP35 are lit while the pins are high, and USB serial prints
  `>>> GP0-31 HIGH` / `low` on each change. Plain GPIO, no pixel data.
  `-DPIN_TEST_HALF_PERIOD_MS=` changes the period. Build it for `led_processor_board`.

- `examples/rm2_pins`: RM2 (U5) joint check, no cyw43 driver. First a pull test with the module held
  off (GP43 low): CS, DATA and CLK each read with pull-up then pull-down, and one that doesn't float
  is flagged STUCK LOW/HIGH. Then each line is held high, in pad order (CLK 3, DATA 5 (6 via R14,
  10 via R15), CS 9, WL_ON 12+13), with a `>>> RM2 PIN 3  (CLK)` style label until you press a key;
  probe that castellation for 3.3 V. Build it for `led_processor_board`.

- `examples/psram_test`: stress test for U4, the 8 MB PSRAM (APS6404L, QMI CS1 on GPIO47). Passes
  run forever, alternating 150 MHz and 200 MHz (`-DPSRAM_TEST_FAST_KHZ=250000` to push harder; it
  raises the core to 1.20 V above 200 MHz). Each pass: walking ones/zeros on the data bits, an
  address/wrap check, byte and halfword lanes, all 8 MB filled and checked three ways (uncached,
  written through the cache, read through the cache), all 8 MB by DMA, and both cores hammering
  half the chip each while running from flash. Then speeds in MB/s and
  `>>> PASS 12 @ 200 MHz: OK`. Bad words are listed with the bits that differ (one bit always wrong
  points at an SD0-SD3 joint). At boot it also checks that `__in_psram` data arrives and that an
  `__uninitialized_psram` buffer lands in the PSRAM. The SDK reads U4's ID at boot
  (`PICO_AUTO_DETECT_PSRAM_SIZE`, set by `drivers/psram_stress`), so a dead U4 shows up as
  `>>> NO PSRAM` instead of a crash. Needs pico-sdk 2.3.1 (`hardware_psram`).

- `examples/uber_test`: everything at once, for a soak. Core 1 brings up the RM2, joins WiFi
  (reconnecting if it drops) and gets NTP time every 15 minutes; core 0 writes it to the DS3231
  (printing how far off it was) and shows the time in US Eastern on a status line every second.
  GPIO 0-31 play a slow wave (OUT1 gold, OUT2 red, OUT3 green, OUT4 warm white) at brightness
  16/256 from 100 frames stored in the PSRAM: each frame is DMA'd into one of two SRAM buffers while
  the other goes out, and checked against a checksum, so the PSRAM is tested the way the show would
  use it. The 2 MB the frames don't use get a rolling stress test. Full report every 30 s. WiFi
  credentials come from `wifi_secrets.cmake` if set; otherwise the board asks over USB serial and
  keeps them in the last 4 KB of flash, so a shared .uf2 carries none. Press `w` to change them.
  `-DUBER_BRIGHTNESS=` and `-DUBER_LEDS=0` (leave GPIO 0-31 alone).

Every example turns on USB serial, so a running board always shows up as a tty (strand_test and
all_strands print a status line every 5 s) and `picotool load -f -x file.uf2` can reboot it into
BOOTSEL and flash it without touching the buttons. Keep it that way for new examples.

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
