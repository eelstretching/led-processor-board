"""Swap SW1/SW2 from Pi's 4-pin Wurth 434133025816 symbol to KiCad's 2-pin
Switch:SW_Push with the XKB TS-1088R-02526 (LCSC C455281) footprint.
Text-level edit so the rest of the file keeps KiCad's own formatting."""
import re, sys, uuid

SCH = sys.argv[1]
s = open(SCH).read()

def block(text, start):
    """End index of the s-expression opening at text[start] == '('."""
    d, i, q = 0, start, False
    while True:
        c = text[i]
        if q:
            if c == '\\': i += 1
            elif c == '"': q = False
        elif c == '"': q = True
        elif c == '(': d += 1
        elif c == ')':
            d -= 1
            if d == 0: return i + 1
        i += 1

# 1. SW_Push into lib_symbols
lib = open('/usr/share/kicad/symbols/Switch.kicad_sym').read()
a = lib.index('\t(symbol "SW_Push"\n') + 1
push = lib[a:block(lib, a)].replace('(symbol "SW_Push"', '(symbol "Switch:SW_Push"', 1)
push = '\n'.join('\t' + l for l in push.split('\n'))
if '"Switch:SW_Push"' not in s:
    i = s.index('(lib_symbols') + len('(lib_symbols')
    s = s[:i] + '\n\t' + push + s[i:]

# 2. the two instances
FIELDS = {
    'Value': 'TS-1088R-02526',
    'Footprint': 'Button_Switch_SMD:SW_SPST_TS-1088-xR025',
    'Datasheet': 'https://jlcpcb.com/partdetail/XKB_Connection-TS_1088R02526/C455281',
    'Description': 'Tactile Pushbutton, SPST, N.O., 4x3x2.5mm',
    'manufacturer': 'XKB Connection',
    'manufacturer_pn': 'TS-1088R-02526',
    'size': '4x3x2.5mm', 'package': '4x3x2.5mm',
}
DROP = ('part_uid', 'packaging', 'packaging_qty')  # Pi's part-database fields
OLD_LIB = '(lib_id "RPI_DB:Electromechanical | Switches | Tactile/RP-01H0MT-6E9H8F")'
for ref in ('SW1', 'SW2'):
    i = 0
    while True:
        i = s.index(OLD_LIB, i)
        st = s.rindex('(symbol', 0, i)
        en = block(s, st)
        if '(property "Reference" "%s"' % ref in s[st:en]: break
        i = en
    b = s[st:en].replace(OLD_LIB, '(lib_id "Switch:SW_Push")')
    for k, v in FIELDS.items():
        b = re.sub(r'(\(property "%s" )"(?:[^"\\]|\\.)*"' % re.escape(k), lambda m: m.group(1) + '"%s"' % v, b)
    for k in DROP:
        m = re.search(r'\t\t\(property "%s" ' % k, b)
        if m: b = b[:m.start()] + b[block(b, m.start() + 2):].lstrip('\n')
    at = re.search(r'\(property "Footprint" "[^"]*"\s*\(at [^)]*\)', b).group(0)
    at = re.search(r'\(at [^)]*\)', at).group(0)
    if '(property "LCSC"' not in b:
        j = b.index('\t\t(pin "1"')
        b = b[:j] + ('\t\t(property "LCSC" "C455281"\n\t\t\t%s\n\t\t\t(hide yes)\n\t\t\t(show_name no)\n'
                     '\t\t\t(do_not_autoplace no)\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n'
                     '\t\t\t\t)\n\t\t\t)\n\t\t)\n' % at) + b[j:]
    b = re.sub(r'\t\t\(pin "[34]"\n\t\t\t\(uuid "[^"]*"\)\n\t\t\)\n', '', b)
    s = s[:st] + b + s[en:]

# 3. wiring: the 2-pin symbol's pins sit 1.27 mm inside Pi's, and pins 2/4 go away
def find_wire(a, c):
    for m in re.finditer(r'\t\(wire\b', s):
        e = block(s, m.start() + 1)
        pts = [tuple(map(float, p)) for p in re.findall(r'\(xy ([\d.\-]+) ([\d.\-]+)\)', s[m.start():e])]
        if sorted(pts) == sorted([a, c]): return m.start(), e
    raise KeyError((a, c))

def kill(a, c):
    global s
    st, en = find_wire(a, c)
    s = s[:st] + s[en:].lstrip('\n')

def move(a, c, a2):
    global s
    st, en = find_wire(a, c)
    w = s[st:en]
    w = re.sub(r'\(xy %g %g\)' % a, '(xy %g %g)' % a2, w)
    s = s[:st] + w + s[en:]

def kill_junction(p):
    global s
    m = re.search(r'\t\(junction\n\t\t\(at %g %g\)' % p, s)
    if m:
        s = s[:m.start()] + s[block(s, m.start() + 1):].lstrip('\n')

# SW1 (rotated 90): pins along x 20.32, pin 1 GND side y 194.31, pin 3 side y 181.61
for w in [((22.86, 194.31), (22.86, 195.58)), ((22.86, 195.58), (20.32, 195.58)),
          ((22.86, 181.61), (22.86, 180.34)), ((22.86, 180.34), (20.32, 180.34)),
          # SW2 (mirrored): pin 1 side x 264.16, pin 3 side x 251.46
          ((264.16, 248.92), (265.43, 248.92)), ((265.43, 248.92), (265.43, 246.38)),
          ((251.46, 248.92), (250.19, 248.92)), ((250.19, 248.92), (250.19, 246.38))]:
    kill(*w)
move((20.32, 194.31), (20.32, 195.58), (20.32, 193.04))
move((20.32, 181.61), (20.32, 180.34), (20.32, 182.88))
move((264.16, 246.38), (265.43, 246.38), (262.89, 246.38))
move((251.46, 246.38), (250.19, 246.38), (252.73, 246.38))
for p in [(20.32, 195.58), (20.32, 180.34), (265.43, 246.38), (250.19, 246.38)]:
    kill_junction(p)

# Pi's 4-pin symbol is now unused
k = s.find('\t\t(symbol "RPI_DB:Electromechanical | Switches | Tactile/RP-01H0MT-6E9H8F"')
if k >= 0 and s.count('RP-01H0MT-6E9H8F")') == 0:
    s = s[:k] + s[block(s, k + 2):].lstrip('\n')
open(SCH, 'w').write(s)
