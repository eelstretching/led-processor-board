#!/usr/bin/env python3
"""Second step of the PSRAM move: put back Pi's QSPI escapes from under U3 (top-side stubs and the
vias that used to drop to the bottom U4) so the router can reach the new U4 from them.

usage: psram2.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(IN)


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def trk(net, pts, w=0.15):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1)); t.SetWidth(mm(w)); t.SetLayer(pcbnew.F_Cu)
        t.SetNet(b.FindNet(net)); b.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(b.FindNet(net)); b.Add(v)


via('/QSPI_SD0', 96.6, 90.5)
trk('/QSPI_SD2', [(90.33, 87.83), (93.9, 87.83)]); via('/QSPI_SD2', 93.9, 87.83)
trk('/QSPI_SD3', [(96.6, 86.56), (95.97, 87.2), (92.1, 87.2)]); via('/QSPI_SD3', 92.1, 87.2)
trk('/QSPI_SCLK', [(96.6, 87.83), (95.97, 88.47), (92.1, 88.47)]); via('/QSPI_SCLK', 92.1, 88.47)
# GPIO47 to R11: Pi's escape and drop to the bottom, then the router takes it west
trk('/GPIO47_ADC7', [(104.9, 97.0), (105.8, 97.0), (106.4, 96.4), (108.59, 96.4), (112.2, 92.79), (112.2, 87.5)])
via('/GPIO47_ADC7', 112.2, 87.5)
for (x0, y0), (x1, y1) in (((112.2, 87.5), (105.81, 81.11)), ((105.81, 81.11), (101.5, 81.11))):
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1)); t.SetWidth(mm(0.15)); t.SetLayer(pcbnew.B_Cu)
    t.SetNet(b.FindNet('/GPIO47_ADC7')); b.Add(t)
# C22's ground pad sits on the edge of the top GND pour: give it its own via
trk('GND', [(83.475, 73.6), (83.6, 74.7)], 0.3)
via('GND', 83.6, 74.7)
pcbnew.SaveBoard(OUT, b, True)
