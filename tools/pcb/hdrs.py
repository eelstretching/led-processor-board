#!/usr/bin/env python3
"""Put J9 (RM2 WL_GPIO1/2) and J3 (SWD) side by side in the open area under the board name.
Replaces wlhdr.py's spot beside the RM2 (C23/C24 stay where Pi had them). Rips the SWD tracks;
route.sh routes SWCLK/SWD and WL_GPIO1/2.

usage: hdrs.py in.kicad_pcb out.kicad_pcb       (in = the board before wlhdr.py)
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
SYM_UUID = 'da6c4e4c-3e09-48ab-bdbc-08436853f8dc'      # J9 in led-processor-board.kicad_sch
Y = 131.5                                              # header row
J9_X, J3_X = 92.0, 101.92                              # pin 1 of each, pins run +x


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def net(name):
    n = b.FindNet(name)
    if n is None:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
    return n


for t in list(b.GetTracks()):
    if t.GetNetname() in ('/SWCLK', '/SWD'):
        b.Delete(t)

f = b.FindFootprintByReference('J3')
f.SetOrientationDegrees(90)
f.SetPosition(V(J3_X, Y))

lib = '/usr/share/kicad/footprints/Connector_PinHeader_2.54mm.pretty'
f = pcbnew.FootprintLoad(lib, 'PinHeader_1x03_P2.54mm_Vertical')
f.SetFPIDAsString('Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical')
f.SetReference('J9'); f.SetValue('WL_GPIO')
f.SetPath(pcbnew.KIID_PATH('/' + SYM_UUID))
f.SetSheetname('led-processor-board'); f.SetSheetfile('led-processor-board.kicad_sch')
b.Add(f)
f.SetOrientationDegrees(90)
f.SetPosition(V(J9_X, Y))
f.Value().SetVisible(False)
for p in f.Pads():
    p.SetNet(net({'1': '/WL_GPIO1', '2': '/WL_GPIO2', '3': 'GND'}[p.GetNumber()]))
for p in b.FindFootprintByReference('U5').Pads():
    if p.GetNumber() in ('17', '18'):
        p.SetNet(net('/WL_GPIO2' if p.GetNumber() == '17' else '/WL_GPIO1'))

# pin labels under each header (J3's old ones sat beside it)
for d in list(b.GetDrawings()):
    if d.GetClass() == 'PCB_TEXT' and d.GetText() in ('CLK', 'GND', 'DIO') \
            and abs(M(d.GetPosition().x) - 106.9) < 0.05:
        b.Delete(d)
for x0, labs in ((J9_X, ('WL1', 'WL2', 'GND')), (J3_X, ('CLK', 'GND', 'DIO'))):
    for i, lab in enumerate(labs):
        t = pcbnew.PCB_TEXT(b)
        t.SetText(lab); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(V(0.8, 0.8)); t.SetTextThickness(mm(0.12))
        t.SetPosition(V(x0 + 2.54 * i, Y + 2.2))
        b.Add(t)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
