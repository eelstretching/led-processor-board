"""Straighten GPIO0 (the dogleg left from R9's via) and chamfer GPIO1's
right-angle corner, both on the way to OUT1. Usage: tidy_gpio.py PCB"""
import sys
import pcbnew

PCB = sys.argv[1]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(PCB)
near = lambda p, q: abs(p[0] - q[0]) < 0.004 and abs(p[1] - q[1]) < 0.004

def replace(net, old, new):
    old = list(old)
    w = None
    for t in list(b.GetTracks()):
        if t.GetNetname() != net or t.GetClass() == 'PCB_VIA': continue
        e = [(M(p.x), M(p.y)) for p in (t.GetStart(), t.GetEnd())]
        for a, c in old:
            if (near(e[0], a) and near(e[1], c)) or (near(e[0], c) and near(e[1], a)):
                old.remove((a, c)); w = t.GetWidth(); b.Delete(t)
                break
    assert not old, (net, old)
    for a, c in zip(new, new[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); t.SetEnd(pcbnew.VECTOR2I(mm(c[0]), mm(c[1])))
        t.SetLayer(pcbnew.F_Cu); t.SetWidth(w); t.SetNet(b.FindNet(net)); b.Add(t)

replace('/GPIO0',
        [((85.0, 91.0), (85.0, 83.13)), ((85.0, 83.13), (84.5, 82.63)),
         ((84.0, 82.13), (84.5, 82.63)), ((82.93, 83.2), (84.0, 82.13))],
        [(85.0, 91.0), (85.0, 83.7), (84.5, 83.2), (82.93, 83.2)])
replace('/GPIO1',
        [((84.0, 91.2), (84.0, 86.269)), ((84.0, 86.269), (66.038, 86.269))],
        [(84.0, 91.2), (84.0, 86.769), (83.5, 86.269), (66.038, 86.269)])

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b, True)
