#!/usr/bin/env python3
"""Move J2 (spare GPIO header) from beside the core to the top edge, right of the USB-C.
Rips its old fan-out (GPIO35-42 beyond the core escapes); j2route.py routes the new one.

usage: j2top.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


NETS = {'/GPIO%d' % i for i in range(35, 40)} | {'/GPIO40_ADC0', '/GPIO41_ADC1', '/GPIO42_ADC2'}
doomed = []
for t in b.GetTracks():
    pts = [t.GetPosition()] if t.GetClass() == 'PCB_VIA' else [t.GetStart(), t.GetEnd()]
    if t.GetNetname() in NETS and (t.GetClass() == 'PCB_VIA' or any(M(p.x) > 108.0 for p in pts)):
        doomed.append(t)
    # stitching vias where the header goes
    elif t.GetClass() == 'PCB_VIA' and t.GetNetname() == 'GND' and 115.0 <= M(pts[0].x) <= 142.0 \
            and M(pts[0].y) <= 78.0:
        doomed.append(t)
for t in doomed:
    b.Delete(t)
print('ripped', len(doomed))

# J2's pin 1 also joined the RTC's +3V3 feed (B.Cu) to the core pour (F.Cu): a via does that now
v = pcbnew.PCB_VIA(b)
v.SetPosition(V(118.5, 86.0)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(b.FindNet('+3V3'))
b.Add(v)

# pin 1 (3V3) on the right, GND on the left: GPIO42..35 then run left to right in the same order
# they leave U1, and the ADC pins (40-42) stay at the end away from the buck
f = b.FindFootprintByReference('J2')
f.SetOrientationDegrees(270)
f.SetPosition(V(139.86, 72.6))
ref = f.Reference()
ref.SetPosition(V(142.0, 74.6)); ref.SetTextAngleDegrees(0)

# pin labels: drop the old ones beside the column, add new ones under each pin
labels = ['3V3'] + ['GP%d' % i for i in range(35, 43)] + ['GND']
for d in list(b.GetDrawings()):
    if d.GetClass() == 'PCB_TEXT' and d.GetText() in labels and abs(M(d.GetPosition().x) - 119.9) < 0.05:
        b.Delete(d)
for i, lab in enumerate(labels):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(lab); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(V(0.8, 0.8)); t.SetTextThickness(mm(0.12))
    t.SetTextAngleDegrees(90)
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    t.SetPosition(V(139.86 - 2.54 * i, 74.2))
    b.Add(t)
for p in f.Pads():
    print(p.GetNumber(), p.GetNetname(), '%.2f,%.2f' % (M(p.GetPosition().x), M(p.GetPosition().y)))
b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
