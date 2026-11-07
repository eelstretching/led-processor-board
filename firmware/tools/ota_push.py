#!/usr/bin/env python3
"""Push firmware to a processor board over WiFi (see firmware/ota/README.md).

    ota_push.py procboard-1a2b.local                      # just ask what it's running
    ota_push.py procboard-1a2b.local ota_test_fast.uf2    # push, wait for it to come back
    ota_push.py BOARD a.uf2 b.uf2 --stress 20             # alternate two images 20 times

No packages needed beyond the Python standard library. The board must already be running OTA
firmware (loaded once over USB), with the partition table from firmware/ota/partitions.json.
"""
import argparse
import hashlib
import re
import socket
import struct
import sys
import time

PORT = 4242
MAGIC = 0x3141544F  # 'OTA1'

UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FLAG_NOT_MAIN_FLASH = 0x1
UF2_FLAG_FAMILY = 0x2000
FAMILIES = {0xE48BFF59: "rp2350-arm-s", 0xE48BFF5A: "rp2350-riscv"}


class Image:
    def __init__(self, path):
        self.path = path
        raw = open(path, "rb").read()
        if len(raw) % 512:
            sys.exit(f"{path}: not a UF2 (size isn't a multiple of 512)")
        blocks = {}
        family = None
        for off in range(0, len(raw), 512):
            m0, m1, flags, addr, size, _no, _count, fam = struct.unpack_from("<8I", raw, off)
            (mend,) = struct.unpack_from("<I", raw, off + 508)
            if (m0, m1, mend) != (UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END):
                sys.exit(f"{path}: bad UF2 block at offset {off}")
            if flags & UF2_FLAG_NOT_MAIN_FLASH or not flags & UF2_FLAG_FAMILY or fam not in FAMILIES:
                continue  # e.g. the RP2350-E10 'absolute' block at the top of flash
            if size != 256 or addr % 256:
                sys.exit(f"{path}: unexpected UF2 block (size {size} at {addr:#x})")
            if family is not None and fam != family:
                sys.exit(f"{path}: mixes UF2 families")
            family = fam
            blocks[addr] = raw[off + 32 : off + 32 + 256]
        if not blocks:
            sys.exit(f"{path}: no rp2350 flash blocks in it")
        self.family = family
        self.blocks = sorted(blocks.items())
        data = b"".join(d for _, d in self.blocks)
        self.sha = hashlib.sha256(data).digest()
        m = re.search(rb"OTA_LABEL=([^;\x00]{1,32});", data)
        self.label = m.group(1).decode() if m else None

    def describe(self):
        kb = len(self.blocks) * 256 // 1024
        label = f", label {self.label}" if self.label else ""
        return f"{self.path} ({kb} KB, {FAMILIES[self.family]}{label})"


def parse_hello(line):
    fields = dict(f.split("=", 1) for f in line.split()[1:] if "=" in f)
    return fields


def hello(host, timeout=5.0):
    with socket.create_connection((host, PORT), timeout=timeout) as s:
        return read_line(s)


def read_line(sock):
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = sock.recv(256)
        if not chunk:
            break
        buf += chunk
    return buf.decode(errors="replace").strip()


def push(host, image):
    t0 = time.time()
    with socket.create_connection((host, PORT), timeout=30) as s:
        greeting = read_line(s)
        if not greeting.startswith("OTA1"):
            sys.exit(f"unexpected greeting from {host}: {greeting!r}")
        before = parse_hello(greeting)
        print(f"board: {greeting}")
        if before.get("updates") != "yes":
            sys.exit("board isn't taking updates right now (trial boot?); try again in 20 s")
        s.sendall(struct.pack("<4I", MAGIC, image.family, len(image.blocks), 0) + image.sha)
        total = len(image.blocks)
        step = max(1, total // 40)
        for i, (addr, data) in enumerate(image.blocks, 1):
            s.sendall(struct.pack("<I", addr) + data)
            if i % step == 0 or i == total:
                print(f"\r  sent {i}/{total} blocks", end="", flush=True)
        print()
        reply = read_line(s)
    secs = time.time() - t0
    if not reply.startswith("OK"):
        sys.exit(f"push failed: {reply or 'connection closed'}")
    print(f"  board says {reply} ({secs:.1f} s, {total * 256 / 1024 / secs:.0f} KB/s); rebooting")
    return before


def wait_for_board(host, image, before, timeout=60.0):
    """Waits for the board to come back and reports whether the new image stuck or rolled back."""
    start = time.time()
    deadline = start + timeout
    seen_trial = False
    time.sleep(2)
    while time.time() < deadline:
        try:
            line = hello(host, timeout=3)
        except OSError:
            time.sleep(1)
            continue
        now = parse_hello(line)
        new_image = image.label is None or now.get("label") == image.label
        if now.get("slot") != before.get("slot") and new_image:
            if now.get("state") == "committed":
                print(f"  OK: running {now.get('label')} from slot {now.get('slot')}, committed")
                return True
            if not seen_trial:
                print(f"  running {now.get('label')} from slot {now.get('slot')} as a TRIAL...")
                seen_trial = True
        elif now.get("slot") == before.get("slot") and now.get("label") == before.get("label"):
            if seen_trial or time.time() > start + 25:
                print(f"  ROLLED BACK: board is on {now.get('label')} in slot {now.get('slot')} again")
                return False
        time.sleep(1)
    print("  no answer from the board in time")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", help="board IP address or procboard-xxxx.local")
    ap.add_argument("uf2", nargs="*", help=".uf2 file(s) to push")
    ap.add_argument("--stress", type=int, metavar="N", help="push N times, cycling through the files")
    ap.add_argument("--no-wait", action="store_true", help="don't wait for the board to come back")
    args = ap.parse_args()

    if not args.uf2:
        print(hello(args.host))
        return
    images = [Image(p) for p in args.uf2]
    for im in images:
        print(im.describe())
    runs = args.stress or len(images)
    good = 0
    for n in range(runs):
        image = images[n % len(images)]
        if runs > 1:
            print(f"--- push {n + 1}/{runs}: {image.path}")
        before = push(args.host, image)
        if args.no_wait:
            good += 1
            continue
        if wait_for_board(args.host, image, before):
            good += 1
        elif args.stress:
            time.sleep(20)  # a rollback, or a slow board; let it settle before the next push
    if runs > 1:
        print(f"=== {good}/{runs} pushes came back running the new image")
    sys.exit(0 if good == runs else 1)


if __name__ == "__main__":
    main()
