#!/usr/bin/env python3
"""Move the bottom shrouds (J7 OUT3, J8 OUT4) down toward the board edge, as the left ones are.
Their pins stop just short of the antenna strip (no copper below y 154.3); J8 also shifts 1.6 mm left
to clear MH3's keepout. Re-runs the data lines from the 33R resistors (J8's with a jog), moves the
shroud caps above the shrouds' right ends, moves the OUT labels above the shrouds, and rips the
power/GND copper there for route.sh.

usage: shrouds.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
DY = 5.8
SHIFT = {'J7': 0.0, 'J8': -1.6}


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


data = set()
for r in SHIFT:
    for p in b.FindFootprintByReference(r).Pads():
        if p.GetNetname() not in ('GND', '+3V3', '+5V'):
            data.add(p.GetNetname())


def in_area(p):
    x, y = M(p.x), M(p.y)
    return y > 140.75 and (56.0 < x < 88.0 or 113.5 < x < 147.0)


doomed = []
for t in b.GetTracks():
    n = t.GetNetname()
    pts = [t.GetPosition()] if t.GetClass() == 'PCB_VIA' else [t.GetStart(), t.GetEnd()]
    if n in data or (n in ('GND', '+3V3', '+5V') and any(in_area(p) for p in pts)):
        doomed.append(t)
for t in doomed:
    b.Delete(t)
print('ripped', len(doomed))

for r, dx in SHIFT.items():
    f = b.FindFootprintByReference(r)
    p = f.GetPosition()
    f.SetPosition(V(M(p.x) + dx, M(p.y) + DY))


def track(net, pts, w=0.15, layer=pcbnew.F_Cu):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1))
        t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(b.FindNet(net))
        b.Add(t)


# data lines: 33R pad 1 (y 140.21) straight down to the shroud's even pin
ROW = 144.66 + DY
for r, dx in SHIFT.items():
    for p in b.FindFootprintByReference(r).Pads():
        n = p.GetNetname()
        if n not in data:
            continue
        x = M(p.GetPosition().x) - dx          # the resistor stays put
        if dx:
            track(n, [(x, 140.21), (x, 141.6), (x + dx, 141.6 - dx), (x + dx, ROW)])
        else:
            track(n, [(x, 140.21), (x, ROW)])

# caps: +3V3 then +5V, vertical, above each shroud's right end
for ref, x in (('C40', 83.0), ('C41', 84.8), ('C42', 140.6), ('C43', 142.4)):
    f = b.FindFootprintByReference(ref)
    f.SetOrientationDegrees(90)
    f.SetPosition(V(x, 143.7))

for d in b.GetDrawings():
    if d.GetClass() == 'PCB_TEXT' and d.GetText().startswith(('OUT3', 'OUT4')):
        p = d.GetPosition()
        dx = SHIFT['J8'] if d.GetText().startswith('OUT4') else 0.0
        d.SetPosition(V(M(p.x) + dx, 145.6))

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
