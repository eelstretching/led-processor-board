#!/usr/bin/env python3
"""Last hand fixes on the routed board, then refill.

usage: fix.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(IN)


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def trk(net, pts, w, layer=pcbnew.F_Cu):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1)); t.SetWidth(mm(w)); t.SetLayer(layer)
        t.SetNet(b.FindNet(net)); b.Add(t)


def via(net, x, y, d=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(d)); v.SetDrill(mm(drill)); v.SetNet(b.FindNet(net)); b.Add(v)


# the +5V run to J7 cuts the RM2's 3V3 pour off from the core pour: two B.Cu jumpers under it
for x in (91.6, 93.2):
    via('+3V3', x, 137.6); via('+3V3', x, 140.3)
    trk('+3V3', [(x, 137.6), (x, 140.3)], 0.5, pcbnew.B_Cu)
# C40 (J7's 3V3 decoupling) to J7 pin 13, under the +5V run
trk('+3V3', [(77.76, 153.0), (77.76, 152.3), (77.6, 151.6)], 0.3)
via('+3V3', 77.6, 151.6)
trk('+3V3', [(77.6, 151.6), (78.24, 150.96), (78.24, 147.2)], 0.3, pcbnew.B_Cu)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
