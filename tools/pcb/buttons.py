# Swap Pi's Wurth 434133025816 buttons (not at LCSC) for the XKB TS-1088R-02526
# (LCSC C455281) in the same spots, pads along y, and re-join their stubs.
import sys, pcbnew
IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
P = lambda x, y: pcbnew.VECTOR2I(mm(x), mm(y))
b = pcbnew.LoadBoard(IN)
FP = 'SW_SPST_TS-1088-xR025'
lib = '/usr/share/kicad/footprints/Button_Switch_SMD.pretty'
# ref -> (net on the top pad, net on the bottom pad, rotation putting pad 1
# on the schematic's pin 1 net)
NETS = {'SW1': ('GND', '/~{USB_BOOT}', 270), 'SW2': ('/RUN', 'Net-(R4-Pad1)', 90)}
for ref, (top, bot, rot) in NETS.items():
    old = b.FindFootprintByReference(ref)
    f = pcbnew.FootprintLoad(lib, FP)
    f.SetReference(ref); f.SetValue('TS-1088R-02526'); f.SetPath(old.GetPath())
    f.SetSheetname(old.GetSheetname()); f.SetSheetfile(old.GetSheetfile())
    f.SetFPIDAsString('Button_Switch_SMD:' + FP)
    b.Add(f)
    f.SetPosition(old.GetPosition()); f.SetOrientationDegrees(rot)
    f.Reference().SetVisible(True); f.Value().SetVisible(False)
    f.Reference().SetPosition(old.Reference().GetPosition())
    f.Reference().SetTextAngleDegrees(old.Reference().GetTextAngleDegrees())
    for p in f.Pads():
        p.SetNet(b.FindNet(top if p.GetPosition().y < f.GetPosition().y else bot))
    b.Delete(old)

# R4's ref would sit inside SW2's outline: move it to R4's left
r4 = b.FindFootprintByReference('R4').Reference()
r4.SetPosition(P(106.9, 113.7)); r4.SetTextAngleDegrees(90)
# and the switch refs clear of the wider TS-1088 outlines
b.FindFootprintByReference('SW1').Reference().SetPosition(P(88.6, 74.6))
b.FindFootprintByReference('SW2').Reference().SetPosition(P(114.1, 113.6))

def seg(a, c, net, w=0.15):
    t = pcbnew.PCB_TRACK(b); t.SetStart(P(*a)); t.SetEnd(P(*c))
    t.SetLayer(pcbnew.F_Cu); t.SetWidth(mm(w)); t.SetNet(b.FindNet(net)); b.Add(t)

# old stubs that ran along the Wurth pad columns
DEAD = {
    'GND': [((92.275, 73.125), (92.275, 75.075)), ((92.275, 75.075), (92.275, 77.275))],
    '/~{USB_BOOT}': [((90.125, 77.275), (90.125, 73.125))],
    '/RUN': [((111.8, 111.525), (111.8, 115.675)), ((111.8, 111.0), (111.8, 111.525))],
    'Net-(R4-Pad1)': [((109.65, 111.525), (109.725, 111.525)), ((109.725, 111.525), (109.725, 113.2)),
                      ((109.725, 115.675), (109.65, 115.675)), ((109.725, 113.2), (109.725, 115.675))],
}
key = lambda a, c: tuple(sorted([(round(a[0], 3), round(a[1], 3)), (round(c[0], 3), round(c[1], 3))]))
for t in list(b.GetTracks()):
    if t.GetClass() != 'PCB_TRACK' or t.GetNetname() not in DEAD: continue
    s, e = t.GetStart(), t.GetEnd()
    k = key((M(s.x), M(s.y)), (M(e.x), M(e.y)))
    if k in [key(*d) for d in DEAD[t.GetNetname()]]:
        b.Delete(t)

# SW1: pads at (91.2, 72.975) GND and (91.2, 77.425) USB_BOOT
seg((92.275, 75.075), (91.7, 74.5), 'GND', 0.25)
seg((91.7, 74.5), (91.7, 73.0), 'GND', 0.25)
seg((90.125, 77.275), (90.275, 77.425), '/~{USB_BOOT}')
seg((90.275, 77.425), (91.2, 77.425), '/~{USB_BOOT}')
# SW2: pads at (110.725, 111.375) RUN and (110.725, 115.825) R4
seg((111.8, 111.0), (111.425, 111.375), '/RUN')
seg((109.725, 113.2), (109.725, 115.0), 'Net-(R4-Pad1)')
seg((109.725, 115.0), (110.2, 115.475), 'Net-(R4-Pad1)')

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
for ref in NETS:
    for p in b.FindFootprintByReference(ref).Pads():
        print(ref, p.GetNumber(), round(M(p.GetPosition().x), 3), round(M(p.GetPosition().y), 3), p.GetNetname())
