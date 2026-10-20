#!/usr/bin/env python3
"""Add J9, a 1x3 header for the RM2's spare WL_GPIO1 / WL_GPIO2 (plus GND), left of the module
between J7 and U5. C23/C24 move up to make room; WL_GPIO2 runs straight across on F.Cu and
WL_GPIO1 hops under it on B.Cu.

usage: wlhdr.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
F, B = pcbnew.F_Cu, pcbnew.B_Cu
SYM_UUID = 'da6c4e4c-3e09-48ab-bdbc-08436853f8dc'      # J9 in led-processor-board.kicad_sch


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def net(name):
    n = b.FindNet(name)
    if n is None:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
    return n


def track(name, layer, pts, w):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1))
        t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(net(name))
        b.Add(t)


def via(name, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(net(name))
    b.Add(v)


# C23/C24's old supply and ground hookup
doomed = []
for t in b.GetTracks():
    pts = [t.GetPosition()] if t.GetClass() == 'PCB_VIA' else [t.GetStart(), t.GetEnd()]
    p_old = [(89.2, 146.55), (89.2, 148.45), (91.3, 146.92), (91.3, 147.88), (89.2, 149.7), (91.3, 148.9),
             (91.3, 146.05), (89.2, 146.05)]
    if t.GetNetname() in ('+3V3', 'GND') and any(abs(M(p.x) - x) < 0.02 and abs(M(p.y) - y) < 0.02
                                                 for p in pts for x, y in p_old):
        doomed.append(t)
for t in doomed:
    b.Delete(t)
print('ripped', len(doomed))

# rot 90: pad 1 (+3V3) at the bottom
for ref, x, y in (('C23', 88.0, 143.4), ('C24', 90.3, 142.6)):
    f = b.FindFootprintByReference(ref)
    f.SetOrientationDegrees(90)
    f.SetPosition(V(x, y))
track('+3V3', F, [(93.25, 146.05), (90.95, 146.05), (90.3, 145.4), (90.3, 143.08)], 0.4)
track('+3V3', F, [(90.3, 145.4), (88.0, 145.4), (88.0, 144.35)], 0.4)
track('GND', F, [(88.0, 142.45), (88.3, 141.2)], 0.4)
via('GND', 88.3, 141.2)
track('GND', F, [(90.3, 142.12), (90.3, 141.1)], 0.3)
via('GND', 90.3, 141.1)

# J9: pin 1 WL_GPIO1, pin 2 WL_GPIO2, pin 3 GND, top to bottom
lib = '/usr/share/kicad/footprints/Connector_PinHeader_2.54mm.pretty'
f = pcbnew.FootprintLoad(lib, 'PinHeader_1x03_P2.54mm_Vertical')
f.SetFPIDAsString('Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical')
f.SetReference('J9'); f.SetValue('WL_GPIO')
f.SetPath(pcbnew.KIID_PATH('/' + SYM_UUID))
f.SetSheetname('led-processor-board'); f.SetSheetfile('led-processor-board.kicad_sch')
b.Add(f)
f.SetPosition(V(89.6, 147.55))
f.Value().SetVisible(False)
for p in f.Pads():
    p.SetNet(net({'1': '/WL_GPIO1', '2': '/WL_GPIO2', '3': 'GND'}[p.GetNumber()]))
for p in b.FindFootprintByReference('U5').Pads():
    if p.GetNumber() in ('17', '18'):
        p.SetNet(net('/WL_GPIO2' if p.GetNumber() == '17' else '/WL_GPIO1'))

track('/WL_GPIO2', F, [(93.25, 147.55), (91.95, 147.55), (91.0, 148.5), (91.0, 149.2), (90.11, 150.09),
                       (89.6, 150.09)], 0.2)
track('/WL_GPIO1', F, [(93.25, 149.05), (91.9, 149.6)], 0.2)
via('/WL_GPIO1', 91.9, 149.6)
track('/WL_GPIO1', B, [(91.9, 149.6), (91.9, 148.3), (91.15, 147.55), (89.6, 147.55)], 0.2)

# pin labels in the gap left of the header
for lab, y in (('WL1', 147.55), ('WL2', 150.09), ('GND', 152.63)):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(lab); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(V(0.8, 0.8)); t.SetTextThickness(mm(0.12))
    t.SetTextAngleDegrees(90)
    t.SetPosition(V(87.25, y))
    b.Add(t)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
