# Over-the-air updates

The processor board's 16 MB flash gets an RP2350 partition table with two firmware slots, A and B.
The board runs from one slot while a new image is pushed over WiFi into the other, then reboots
into it. The bootrom's "try before you buy" (TBYB) feature makes that safe: a freshly pushed image
gets one trial boot under a 16.7 s watchdog, and unless it commits itself in that time the board
falls back to the image it had before. Our rule is that firmware only commits once WiFi is up and
its update server is listening, so a pushed image that can't take the *next* update never sticks.

No bootloader of our own is involved. The A/B choice, the trial and the rollback are all done by
the bootrom.

## Flash layout

`partitions.json` holds the table. Offsets are from the start of flash:

| # | Name | Start | Size | Used for |
|---|---|---|---|---|
| | partition table | 0 | 8 KB | written once over USB |
| 0 | Firmware A | 8 KB | 4088 KB | firmware (slot A) |
| 1 | Firmware B | 4 MB | 4 MB | firmware (slot B), linked to A |
| 2 | WiFi firmware A | 8 MB | 256 KB | reserved: CYW43 blob, for smaller pushes later |
| 3 | WiFi firmware B | 8.25 MB | 256 KB | reserved, linked to 2 |
| 4 | Settings | 8.5 MB | 64 KB | reserved: WiFi credentials, prop name, PicoComm priority |
| 5 | Show data | 8.56 MB | 7600 KB | reserved: show scripts, palettes, images |
| | unpartitioned | 16368 KB | 16 KB | RP2350-E10 workaround block and BTstack's flash bank |

Today's firmware is about 600 KB, so a 4 MB slot leaves plenty of room. The reserved partitions
cost nothing now, and they mean the table never has to change, because changing it means
re-flashing every board over USB. The top 16 KB stays unpartitioned because the RP2350-E10 UF2
workaround writes to the last page of flash, and `picotool partition create` refuses a table that
covers it.

Every existing .uf2 (pin_test, all_strands, bringup...) still loads over USB on a partitioned
board. It lands in a free slot and boots as usual. A plain (non-TBYB) image loaded that way erases
the other slot, so loading one of those wipes the OTA firmware. To get OTA back, load an OTA build
over USB again. The partition table itself stays.

## Which slot boots

The bootrom does the bookkeeping. These rules come from the bootrom source
(`varm_flash_boot.c`, `s_varm_crit_choose_by_tbyb_flash_update_boot_and_version`):

- After a push, the firmware reboots with a "flash update" boot that names the new slot. The bootrom
  tries that slot first, and because the image is TBYB-flagged it arms a 16.7 s watchdog.
- `ota::commit()` (the bootrom's `rom_explicit_buy`) stops the watchdog and clears the image's TBYB
  flag. It also erases the start of the old slot when the new image isn't a higher version.
  We don't set versions, so every image counts as the same version, and after a commit only the
  new image is valid.
- If there's no commit, the watchdog resets the board. On that boot an un-bought TBYB image isn't
  bootable, so the bootrom runs the old slot again.

So version numbers aren't needed, and you never have to bump one. The other side of that is that
once an image commits, the old one is gone. A board that commits an image and then misbehaves
later goes back to the USB route.

## The pieces

- `drivers/ota` (the `ota` library): `ota::bootState()`, `ota::commit()`, and `ota::Server`, a
  lwIP raw-API TCP server on port 4242. It writes into the slot the bootrom says a UF2 of that
  family should go to, which is never the running one (it checks). It reads every 256-byte block
  back from flash, checks a SHA-256 of the whole stream, and only then answers `OK` and asks for
  the flash-update reboot. A push that fails or stalls for 10 s erases the half-written slot.
- `examples/ota_test`: three builds of one test program. `ota_test_slow` blinks D1 and GP35 once
  a second, `ota_test_fast` about three times a second, and `ota_test_nobuy` flickers and never
  commits. The board answers to `procboard-xxxx.local` (mDNS, from the chip's unique ID) and
  prints a status line over USB every 5 s.
- `tools/ota_push.py`: the laptop side, standard library only. It reads the .uf2, streams it,
  then waits for the board to come back and says whether the new image stuck or rolled back.

The wire format is in `drivers/ota/ota.cpp`. After the board's hello line, the laptop sends a
48-byte header and then (address, 256 bytes) records, all little-endian.

## Trying it

The ota_test builds bake in the WiFi credentials from `firmware/wifi_secrets.cmake` (see
`firmware/README.md`), so build them on the Mac. Don't share those .uf2s.

```sh
# 1. Build (from the firmware directory, as for the other examples)
cmake -S . -B build -G Ninja
ninja -C build ota_test_slow ota_test_fast ota_test_nobuy

# 2. Once per board, over USB: write the partition table, then the first OTA image.
#    (Or use ota_partitions.uf2 from the project files instead of creating it.)
picotool partition create ota/partitions.json build/ota_partitions.uf2
picotool reboot -u -f                 # running firmware -> BOOTSEL (or hold BOOTSEL, plug USB)
picotool load build/ota_partitions.uf2
picotool reboot -u                    # reboot so the bootrom picks up the table, back to BOOTSEL
picotool partition info               # should list Firmware A/B, WiFi firmware A/B, Settings, Show data
picotool load -x build/examples/ota_test/ota_test_slow.uf2
tio /dev/cu.usbmodem*                 # wait for ">>> COMMITTED" and the board's IP / name
```

Then from the laptop, over WiFi only (the USB cable can stay in for `tio`, or not):

```sh
python3 tools/ota_push.py procboard-xxxx.local                    # what's it running?
python3 tools/ota_push.py procboard-xxxx.local build/examples/ota_test/ota_test_fast.uf2
python3 tools/ota_push.py procboard-xxxx.local build/examples/ota_test/ota_test_nobuy.uf2
python3 tools/ota_push.py procboard-xxxx.local \
    build/examples/ota_test/ota_test_slow.uf2 build/examples/ota_test/ota_test_fast.uf2 --stress 20
```

What you should see:

1. The `fast` push runs from slot B and blinks quickly. `ota_push.py` reports it committed.
2. The `nobuy` push flickers for about 17 s. Then the board resets on its own, goes back to `fast`
   in slot B, and `ota_push.py` reports a rollback.
3. The stress run flips A, B, A, B... and ends with `=== 20/20 pushes came back running the new
   image`.

If the first USB load never reaches `COMMITTED` (wrong WiFi password, router off), the trial
times out and there's nothing older to fall back to, so the board comes up in BOOTSEL. Fix the
credentials and load it again.

## Next steps

1. Put `ota::Server` into the show firmware. PicoComm's `Node` already runs lwIP in poll mode on
   core 1, so the server goes there. Core 0 must call `flash_safe_execute_core_init()` (or
   `multicore_lockout_victim_init()`), because each 4 KB erase stalls the core rendering from
   flash for about 50 ms. Pushes should be refused while a show is running.
2. Move the CYW43 blob into the reserved WiFi firmware partitions
   (`pico_use_wifi_firmware_partition`), which cuts about 230 KB from every push.
3. Keep WiFi credentials and the prop's name and priority in the Settings partition, so one
   .uf2 serves every board.
4. Anyone on the show network can push. That's fine on a dedicated WPA2 show router. Signed images
   (`pico_sign_binary` plus OTP key hashes) would close that gap, but burning OTP can't be undone,
   so leave it alone this season.
