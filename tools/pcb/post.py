#!/usr/bin/env python3
"""After autorouting: trim dangling trace tails, stitch the GND pours, refill.

usage: post.py in.kicad_pcb out.kicad_pcb [--trim-only]
"""
import sys, math
import pcbnew

IN, OUT = sys.argv[1:3]
TRIM_ONLY = '--trim-only' in sys.argv
mm = pcbnew.FromMM
MM = pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
TOL = mm(0.005)
KEEP_NETS = {'GND', '+3V3', '+1V1', '/VREG_LX', '/VREG_AVDD'}   # pour nets: tracks may end in copper


def same(a, c):
    return abs(a.x - c.x) <= TOL and abs(a.y - c.y) <= TOL


# ---------------------------------------------------------------- dangling tails
pads = [p for f in b.GetFootprints() for p in f.Pads()]
removed = 0
while True:
    tracks = list(b.GetTracks())
    segs = [t for t in tracks if t.GetClass() == 'PCB_TRACK']
    vias = [t for t in tracks if t.GetClass() == 'PCB_VIA']
    doomed = []
    for s in segs:
        if s.GetNetname() in KEEP_NETS:
            continue
        nc, lay = s.GetNetCode(), s.GetLayer()
        for e in (s.GetStart(), s.GetEnd()):
            if any(p.GetNetCode() == nc and p.IsOnLayer(lay) and p.HitTest(e) for p in pads):
                continue
            if any(v.GetNetCode() == nc and v.HitTest(e) for v in vias):
                continue
            # touching another segment of the net: at its end, or anywhere along it (T-junction)
            if any(o is not s and o.GetNetCode() == nc and o.GetLayer() == lay and o.HitTest(e, 1) for o in segs):
                continue
            doomed.append(s)
            break
    # vias with nothing on them
    for v in vias:
        if v.GetNetname() in KEEP_NETS:
            continue
        p = v.GetPosition()
        if not any(s.GetNetCode() == v.GetNetCode() and (same(s.GetStart(), p) or same(s.GetEnd(), p)) for s in segs if s not in doomed):
            doomed.append(v)
    if not doomed:
        break
    for t in doomed:
        b.Delete(t)
    removed += len(doomed)
print('trimmed', removed)

# ---------------------------------------------------------------- GND stitching
gnd = b.FindNet('GND')
VIA_D, VIA_DRILL, CLR = 0.6, 0.3, 0.3
R = VIA_D / 2 + CLR
obst_segs = []
for t in b.GetTracks():
    if t.GetClass() == 'PCB_VIA':
        p = t.GetPosition()
        obst_segs.append((MM(p.x), MM(p.y), MM(p.x), MM(p.y), MM(t.GetWidth(pcbnew.F_Cu)) / 2))
    else:
        s, e = t.GetStart(), t.GetEnd()
        obst_segs.append((MM(s.x), MM(s.y), MM(e.x), MM(e.y), MM(t.GetWidth()) / 2))
boxes = []
for f in b.GetFootprints():
    bb = f.GetBoundingBox(False)
    boxes.append((MM(bb.GetLeft()), MM(bb.GetTop()), MM(bb.GetRight()), MM(bb.GetBottom())))
for d in b.GetDrawings():     # keep vias out of silk labels (the fab clips silk over holes)
    if d.GetClass() == 'PCB_TEXT' and d.GetLayer() == pcbnew.F_SilkS:
        bb = d.GetBoundingBox()
        boxes.append((MM(bb.GetLeft()), MM(bb.GetTop()), MM(bb.GetRight()), MM(bb.GetBottom())))
for z in b.Zones():
    if z.GetIsRuleArea():
        bb = z.GetBoundingBox()
        boxes.append((MM(bb.GetLeft()), MM(bb.GetTop()), MM(bb.GetRight()), MM(bb.GetBottom())))


def segdist(px, py, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((px - x0) * dx + (py - y0) * dy) / L))
    return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))


def free(x, y):
    for x0, y0, x1, y1 in boxes:
        if x0 - R <= x <= x1 + R and y0 - R <= y <= y1 + R:
            return False
    for x0, y0, x1, y1, hw in obst_segs:
        if segdist(x, y, x0, y0, x1, y1) < R + hw:
            return False
    return True


added = 0
STEP = 5.0
for xa, xb in (() if TRIM_ONLY else ((51.5, 82.5), (117.5, 148.5))):
    y = 72.0
    while y <= 153.0:
        x = xa
        while x <= xb:
            if free(x, y):
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
                v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_DRILL))
                v.SetNet(gnd)
                b.Add(v)
                obst_segs.append((x, y, x, y, VIA_D / 2))
                added += 1
            x += STEP
        y += STEP
print('stitch vias', added)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
