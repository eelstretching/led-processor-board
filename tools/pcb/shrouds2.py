#!/usr/bin/env python3
"""Hand-route the moved J7/J8 shrouds' power (after shrouds.py). The bottom rows now sit by the edge,
so 3V3 and 5V run along the channel between each shroud's two rows (y 151.73), one net per layer:
J7: 5V on F.Cu (on to J6's cap, which feeds J5/J6), 3V3 on B.Cu up to C40 and the RM2-end pour.
J8: 5V on F.Cu (on to the J7 trunk at y 138.74) plus the buck's B.Cu trunk down x 140.14,
    3V3 on B.Cu to the RM2-end pour and up to C42.

usage: shrouds2.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
F, B = pcbnew.F_Cu, pcbnew.B_Cu
CH = 151.73          # channel between the rows (150.46 / 153.00)


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def track(net, layer, pts, w):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1))
        t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(b.FindNet(net))
        b.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(b.FindNet(net))
    b.Add(v)


def place(ref, x):
    f = b.FindFootprintByReference(ref)
    f.SetOrientationDegrees(90)          # pad 1 (supply) at y 144.18, pad 2 (GND) at 143.22
    f.SetPosition(V(x, 143.7))


# ---- J7 (pins 13 = 3V3 at 78.24, 15 = 5V at 80.78)
place('C41', 83.0); place('C40', 84.8)
track('+5V', F, [(57.82, 139.8), (57.82, 151.0), (58.55, CH), (80.78, CH), (80.78, 153.0)], 0.4)
track('+5V', F, [(80.78, 153.0), (82.3, 151.48), (82.3, 146.0), (83.0, 145.3), (83.0, 144.18)], 0.4)
via('+5V', 82.3, 146.0)
track('+5V', B, [(82.3, 146.0), (89.24, 139.06), (89.24, 138.74)], 0.5)
via('+5V', 89.24, 138.74)
track('+3V3', B, [(78.24, 153.0), (79.51, CH), (79.51, 147.0), (84.8, 147.0)], 0.3)
via('+3V3', 84.8, 147.0)
track('+3V3', F, [(84.8, 147.0), (84.8, 144.18)], 0.3)

# ---- J8 (pins 13 = 3V3 at 134.64, 15 = 5V at 137.18)
place('C42', 139.2); place('C43', 141.4)
track('+5V', F, [(108.33, 138.74), (118.12, 148.54), (118.12, 151.0), (118.85, CH), (137.18, CH),
                 (137.18, 153.0)], 0.4)
track('+5V', B, [(140.14, 107.59), (140.14, 152.2), (139.34, 153.0), (137.18, 153.0)], 0.5)
track('+5V', F, [(141.4, 144.18), (141.4, 145.1)], 0.4)
via('+5V', 141.4, 145.1)
track('+5V', B, [(141.4, 145.1), (140.14, 146.36)], 0.4)
track('+3V3', B, [(134.64, 153.0), (133.37, CH), (116.2, CH)], 0.3)
via('+3V3', 116.2, CH)
track('+3V3', B, [(134.64, 153.0), (135.91, CH), (135.91, 147.0), (138.6, 147.0)], 0.3)
via('+3V3', 138.6, 147.0)
track('+3V3', F, [(138.6, 147.0), (139.2, 146.4), (139.2, 144.18)], 0.3)

# cap grounds straight down to the plane
for x in (83.0, 84.8, 139.2, 141.4):
    track('GND', F, [(x, 143.22), (x, 142.3)], 0.3)
    via('GND', x, 142.3)

# the bottom-row GND pins sit between the channel and the antenna strip, so the pours only reach
# them in slivers: tie each shroud's GND pins together along the row
track('GND', F, [(63.0, 153.0), (75.7, 153.0)], 0.4)
track('GND', F, [(119.4, 153.0), (132.1, 153.0)], 0.4)

# the row ends next to the 3V3 pin (J7) and the 5V run (J8) only get one top-side spoke: pour them solid
for ref, num in (('J7', '11'), ('J8', '1')):
    for p in b.FindFootprintByReference(ref).Pads():
        if p.GetNumber() == num:
            p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

# the old B.Cu 5V trunk's leftover stub at its top end
for t in list(b.GetTracks()):
    if t.GetClass() == 'PCB_TRACK' and t.GetNetname() == '+5V' and t.GetLayer() == B:
        if t.GetLength() < mm(0.3) and abs(M(t.GetStart().x) - 140.14) < 0.05:
            b.Delete(t)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
