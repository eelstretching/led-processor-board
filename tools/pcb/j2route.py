#!/usr/bin/env python3
"""Hand-route the top-edge J2 header (after j2top.py): GPIO35-42 leave U1 on F.Cu in Pi's escape
pattern, run right in nested L's to a column of vias at y 92.3 (left of the buck's C30/L2), then fan
out on B.Cu to the header pins. The ADC lanes (40-42) are the leftmost, furthest from the switch
node. The +3V3 hop that joins the core pour to the RTC (and through it the lower pour feeding the RM2)
used to run up B.Cu at x 120.4 to y 86, across the new bus; it now dives through a via at (116.2, 95.5),
just below the via row.

usage: j2route.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def close(p, x, y, tol=0.05):
    return abs(M(p.x) - x) < tol and abs(M(p.y) - y) < tol


# upper end of the old RTC +3V3 hop, plus GND stitching vias in the bus area
doomed = []
for t in b.GetTracks():
    n = t.GetNetname()
    if t.GetClass() == 'PCB_VIA':
        p = t.GetPosition()
        if n == '+3V3' and close(p, 118.5, 86.0):
            doomed.append(t)
        elif n == 'GND' and 115.8 <= M(p.x) <= 124.0 and 76.0 <= M(p.y) <= 104.0:
            doomed.append(t)
    elif n == '+3V3':
        s, e = t.GetStart(), t.GetEnd()
        if any(close(p, 120.4, 86.0) or close(p, 118.5, 86.0) for p in (s, e)):
            doomed.append(t)
for t in doomed:
    b.Delete(t)
print('ripped', len(doomed))


def track(net, layer, pts, w=0.15):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1))
        t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(b.FindNet(net))
        b.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(b.FindNet(net))
    b.Add(v)


# (net, escape from the U1 pin to where the lane starts running right)
ESC = [
    ('/GPIO42_ADC2', [(104.9, 99.0), (106.3, 99.0), (106.9, 98.4)]),
    ('/GPIO41_ADC1', [(104.9, 99.4), (106.4, 99.4), (107.0, 98.8)]),
    ('/GPIO40_ADC0', [(104.9, 100.6), (106.2, 100.6), (106.7, 101.1)]),
    ('/GPIO39', [(104.9, 101.0), (106.1, 101.0), (106.6, 101.5)]),
    ('/GPIO38', [(104.9, 101.4), (106.0, 101.4), (106.5, 101.9)]),
    ('/GPIO37', [(104.9, 101.8), (105.9, 101.8), (106.4, 102.3)]),
    ('/GPIO36', [(104.9, 102.2), (105.8, 102.2), (106.3, 102.7)]),
    ('/GPIO35', [(104.9, 102.6), (105.7, 102.6), (106.2, 103.1)]),
]
VY, EY = 92.3, 75.5     # via row; where the B.Cu diagonals meet the pin columns
for i, (net, pts) in enumerate(ESC):
    c = 117.0 + 0.9 * i           # via column
    p = 119.54 + 2.54 * i         # header pin x (pin 9 = GPIO42 ... pin 2 = GPIO35)
    y = pts[-1][1]
    track(net, pcbnew.F_Cu, pts + [(c - 0.5, y), (c, y - 0.5), (c, VY)])
    via(net, c, VY)
    track(net, pcbnew.B_Cu, [(c, VY), (c, EY + p - c), (p, EY), (p, 72.6)])

# RTC +3V3 hop: core pour -> via -> B.Cu under the lanes -> the old via at (120.4, 115)
via('+3V3', 116.2, 95.5)
track('+3V3', pcbnew.B_Cu, [(116.2, 95.5), (120.4, 99.7), (120.4, 115.0)], w=0.5)

# pin 1 (+3V3) from the core pour along the top edge
track('+3V3', pcbnew.F_Cu, [(116.0, 71.1), (139.36, 71.1), (139.86, 71.6), (139.86, 72.6)], w=0.3)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
