#!/usr/bin/env python3
"""Hand-route SWCLK/SWD to J3's new spot under the board name (after hdrs.py + route.sh, which
couldn't get them out of the core). Same escapes as before from U1 pins 33/34, then straight down,
hopping under the GPIO24-27 run (y 118-118.9) on B.Cu.

usage: swd.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(IN)
F, B = pcbnew.F_Cu, pcbnew.B_Cu


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def track(net, layer, pts, w=0.2):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1))
        t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(b.FindNet(net))
        b.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(b.FindNet(net))
    b.Add(v)


for t in list(b.GetTracks()):
    if t.GetNetname() in ('/SWCLK', '/SWD'):
        b.Delete(t)
for net, x, esc, end in (('/SWCLK', 104.7, [(101.0, 104.9), (101.0, 106.3), (104.7, 110.0)],
                          [(101.92, 122.68), (101.92, 131.5)]),
                         ('/SWD', 106.2, [(101.4, 104.9), (101.4, 106.19), (106.2, 110.99)],
                          [(106.2, 128.1), (107.0, 128.9), (107.0, 131.5)])):
    track(net, F, esc + [(x, 116.8)])
    via(net, x, 116.8)
    track(net, B, [(x, 116.8), (x, 119.9)])
    via(net, x, 119.9)
    track(net, F, [(x, 119.9)] + end)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
