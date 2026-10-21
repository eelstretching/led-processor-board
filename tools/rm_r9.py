"""Drop R9, Pi's unfitted 0R option from GPIO0 to the flash chip select, from
the schematic and the board, with its wiring and the dead GPIO0 branch to it.
R10/R1 and the rest of FLASH_SS stay. Usage: rm_r9.py SCH PCB"""
import re, sys
import pcbnew

SCH, PCB = sys.argv[1:3]

def block(text, start):
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

s = open(SCH).read()

def cut(st):
    global s
    en = block(s, st)
    while s[st - 1] == '\t': st -= 1
    s = s[:st] + s[en:].lstrip('\n')

# the symbol
i = s.index('(property "Reference" "R9"')
cut(s.rindex('(symbol\n', 0, i))

def find(kind, pat):
    for m in re.finditer(r'\(%s\b' % kind, s):
        if re.search(pat, s[m.start():block(s, m.start())]): return m.start()
    raise KeyError((kind, pat))

for a, c in [((16.51, 143.51), (27.94, 143.51)), ((33.02, 143.51), (36.83, 143.51)),
             ((36.83, 143.51), (36.83, 152.4))]:
    cut(find('wire', r'\(xy %g %g\) \(xy %g %g\)' % (a + c)))
cut(find('label', r'^\(label "GPIO0"\s*\(at 16.51 143.51 '))
cut(find('junction', r'\(at 36.83 152.4\)'))
open(SCH, 'w').write(s)

mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(PCB)
b.Delete(b.FindFootprintByReference('R9'))
DEAD = [  # GPIO0: via at (85.0, 83.1), B.Cu to (90.9, 81.0), via, up to R9 pad 1
    ('via', (85.0, 83.1), (85.0, 83.1)), ('via', (90.9, 81.0), (90.9, 81.0)),
    ('B.Cu', (87.1, 81.0), (85.0, 83.1)), ('B.Cu', (87.1, 81.0), (90.9, 81.0)),
    ('F.Cu', (90.9, 81.0), (90.9, 81.825)),
    ('F.Cu', (84.5, 82.63), (84.53, 82.63)), ('F.Cu', (84.53, 82.63), (85.0, 83.1)),
]
near = lambda p, q: abs(p[0] - q[0]) < 0.004 and abs(p[1] - q[1]) < 0.004
for t in list(b.GetTracks()):
    if t.GetNetname() != '/GPIO0': continue
    kind = 'via' if t.GetClass() == 'PCB_VIA' else b.GetLayerName(t.GetLayer())
    e = [(M(p.x), M(p.y)) for p in (t.GetStart(), t.GetEnd())]
    for d in DEAD:
        if d[0] == kind and ((near(e[0], d[1]) and near(e[1], d[2])) or (near(e[0], d[2]) and near(e[1], d[1]))):
            DEAD.remove(d); b.Delete(t)
            break
assert not DEAD, DEAD
b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b, True)
