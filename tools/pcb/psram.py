#!/usr/bin/env python3
"""Move the PSRAM (U4) and its parts (C22, R11, R13) from under the flash to the top side, left of
the core, so JLCPCB can assemble one side. Rips their old bottom-side routing; route.sh reconnects.

usage: psram.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)

PLACE = {'U4': (79.5, 78.0, 0), 'C22': (83.0, 73.6, 0), 'R13': (73.0, 75.2, 0), 'R11': (73.0, 77.4, 0)}
OLD = (86.0, 79.5, 98.0, 91.0)            # where they sat on the bottom, under U3


def inside(p, r):
    return r[0] <= M(p.x) <= r[2] and r[1] <= M(p.y) <= r[3]


doomed = []
for t in b.GetTracks():
    n = t.GetNetname()
    if n in ('/PSRAM_CS', '/GPIO47_ADC7'):
        doomed.append(t)                  # short, simplest to reroute whole
    elif n.startswith('/QSPI_') or n == '+3V3':
        # the bottom-side branches (and their vias) that fed the old U4/C22/R13 pads
        if t.GetClass() == 'PCB_VIA':
            if n.startswith('/QSPI_') and inside(t.GetPosition(), OLD):
                doomed.append(t)
        elif t.GetLayer() == pcbnew.B_Cu and (inside(t.GetStart(), OLD) or inside(t.GetEnd(), OLD)):
            doomed.append(t)
for r, (x, y, rot) in PLACE.items():
    f = b.FindFootprintByReference(r)
    if f.IsFlipped():
        f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    f.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    f.SetOrientationDegrees(rot)
# stitching vias under the new spot
area = (70.5, 71.0, 86.0, 82.0)
for t in b.GetTracks():
    if t.GetClass() == 'PCB_VIA' and t.GetNetname() == 'GND' and inside(t.GetPosition(), area):
        doomed.append(t)
seen = set()
for t in doomed:
    if t.m_Uuid.AsString() not in seen:
        seen.add(t.m_Uuid.AsString())
        b.Delete(t)
print('ripped', len(seen))
b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
