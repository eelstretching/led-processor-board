#!/usr/bin/env python3
"""Prepare a KiCad Specctra DSN for Freerouting: freeze the existing copper and set net classes.

usage: dsn_prep.py in.dsn out.dsn
"""
import re, sys

src, dst = sys.argv[1:3]
s = open(src).read()

# existing wires and vias are fixed: the core is hand-routed and must not move
s = s.replace('(type route)', '(type protect)')

# net classes: (name, width um, clearance um, nets)
CLASSES = [
    ('PWR12', 800, 200, ['+12V', '/Power/VIN_RAW']),
    ('PWR', 500, 200, ['+5V', 'VSYS', 'VBUS']),
    ('PWR3', 400, 150, ['+3V3']),
    ('GNDC', 400, 150, ['GND']),
    ('DATA', 200, 150, ['"Net-(J%d-Pin_%d)"' % (j, p) for j in (5, 6, 7, 8) for p in range(2, 17, 2)]),
]


def unq(n):
    return n.strip('"')


# pull the nets out of kicad_default
m = re.search(r'\(class kicad_default (.*?)\n      \(circuit', s, re.S)
names = m.group(1).split()
moved = {unq(n) for _, _, _, ns in CLASSES for n in ns}
keep = [n for n in names if unq(n) not in moved]
s = s[:m.start(1)] + '\n      '.join(' '.join(keep[i:i + 8]) for i in range(0, len(keep), 8)) + s[m.end(1):]

# add the new classes after kicad_default
m = re.search(r'\(class kicad_default .*?\n    \)\n', s, re.S)
blk = m.group(0)
add = ''
for name, w, c, ns in CLASSES:
    present = [n for n in ns if unq(n) in {unq(x) for x in names}]
    if not present:
        continue
    b = re.sub(r'\(class kicad_default .*?\n      \(circuit', '(class %s %s\n      (circuit' % (name, ' '.join(present)), blk, flags=re.S)
    b = re.sub(r'\(width \d+(\.\d+)?\)', '(width %d)' % w, b)
    b = re.sub(r'\(clearance \d+(\.\d+)?\)', '(clearance %d)' % c, b)
    add += b
s = s[:m.end()] + add + s[m.end():]
open(dst, 'w').write(s)
print('ok')
