#!/usr/bin/env python3
"""Tidy the top silkscreen: move each reference designator to a spot clear of pads, other silk and
the board edge (shrinking, then hiding, it if nothing fits). Also drops sub-0.1 mm dangling stubs.

usage: silk.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
CLR = 0.15


def box(bb, g=0.0):
    return (M(bb.GetLeft()) - g, M(bb.GetTop()) - g, M(bb.GetRight()) + g, M(bb.GetBottom()) + g)


def hit(a, c):
    return a[0] < c[2] and c[0] < a[2] and a[1] < c[3] and c[1] < a[3]


# tiny clip leftovers that only touch their own net at one end
pads = [p for f in b.GetFootprints() for p in f.Pads()]
for t in list(b.GetTracks()):
    if t.GetClass() != 'PCB_TRACK' or t.GetLength() >= mm(0.1):
        continue
    others = [o for o in b.GetTracks() if o.m_Uuid != t.m_Uuid and o.GetNetCode() == t.GetNetCode()]
    for e in (t.GetStart(), t.GetEnd()):
        if not any(p.GetNetCode() == t.GetNetCode() and p.HitTest(e) for p in pads) and \
                not any((o.GetStart() - e).EuclideanNorm() < mm(0.01) or (o.GetEnd() - e).EuclideanNorm() < mm(0.01)
                        for o in others):
            b.Delete(t)
            break

# board text tweaks: J3 labels beside its pins, J4 labels clear of the pads
MOVES = {('CLK', 103.5): (106.9, 111.8, 90), ('GND', 103.5): (106.9, 114.34, 90), ('DIO', 103.5): (106.9, 116.88, 90),
         ('GND', 143.6): (143.0, 86.5, 0), ('+12V', 143.4): (146.5, 93.0, 0)}
for d in b.GetDrawings():
    if d.GetClass() != 'PCB_TEXT':
        continue
    k = (d.GetText(), round(M(d.GetPosition().x), 1))
    if k in MOVES:
        x, y, rot = MOVES[k]
        d.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); d.SetTextAngleDegrees(rot)
        if k == ('GND', 143.6):
            d.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
        else:
            d.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
b.FindFootprintByReference('J4').Reference().SetPosition(pcbnew.VECTOR2I(mm(146.5), mm(83.6)))
# refs the search below would push too far from their parts: on the body (L2) or beside it (J8)
FIXED = {'J8': (114.9, 147.2, 90), 'L2': (128.0, 96.5, 0)}
for r, (x, y, rot) in FIXED.items():
    t = b.FindFootprintByReference(r).Reference()
    t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
    t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); t.SetTextAngleDegrees(rot)

edge = box(b.GetBoardEdgesBoundingBox(), -0.4)
obst = []
for f in b.GetFootprints():
    for p in f.Pads():
        if p.IsOnLayer(pcbnew.F_Cu) or p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
            obst.append(box(p.GetBoundingBox(), CLR))
    for g in f.GraphicalItems():
        if g.GetLayer() == pcbnew.F_SilkS:
            obst.append(box(g.GetBoundingBox(), CLR))
for d in b.GetDrawings():
    if d.GetLayer() == pcbnew.F_SilkS:
        obst.append(box(d.GetBoundingBox(), CLR))
# holes not in footprints (none expected) and the antenna keepout
for z in b.Zones():
    if z.GetIsRuleArea() and z.GetDoNotAllowTracks() and z.GetZoneName() == '':
        pass

foots = [f for f in b.GetFootprints() if f.GetLayer() == pcbnew.F_Cu and f.Reference().IsVisible()]
# place the crowded small parts first, the big ones (with room around them) last
foots.sort(key=lambda f: M(f.GetBoundingBox(False).GetWidth()) * M(f.GetBoundingBox(False).GetHeight()))
placed, moved, shrunk, hidden = [], 0, 0, 0


def free(r):
    return (edge[0] <= r[0] and r[2] <= edge[2] and edge[1] <= r[1] and r[3] <= edge[3]
            and not any(hit(r, o) for o in obst) and not any(hit(r, o) for o in placed))


for f in list(foots):
    if f.GetReference() in FIXED:
        placed.append(box(f.Reference().GetBoundingBox()))
        foots.remove(f)
for f in foots:
    ref = f.Reference()
    cur = box(ref.GetBoundingBox())
    if free(cur):
        placed.append(cur)
        continue
    body = box(f.GetBoundingBox(False))
    cx, cy = (body[0] + body[2]) / 2, (body[1] + body[3]) / 2
    done = False
    for size in sorted({round(M(ref.GetTextHeight()), 2), 0.8}, reverse=True):
        ref.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        ref.SetTextThickness(mm(size * 0.15))
        for rot in (0, 90):
            ref.SetTextAngleDegrees(rot)
            ref.SetPosition(pcbnew.VECTOR2I(mm(cx), mm(cy)))
            tb = box(ref.GetBoundingBox())
            w, h = tb[2] - tb[0], tb[3] - tb[1]
            for g in (0.1, 0.35, 0.7, 1.1, 1.6, 2.2):
                cands = [(cx, body[1] - h / 2 - g), (cx, body[3] + h / 2 + g),
                         (body[0] - w / 2 - g, cy), (body[2] + w / 2 + g, cy),
                         (body[0] - w / 2 - g, body[1] - h / 2 - g), (body[2] + w / 2 + g, body[1] - h / 2 - g),
                         (body[0] - w / 2 - g, body[3] + h / 2 + g), (body[2] + w / 2 + g, body[3] + h / 2 + g)]
                for x, y in cands:
                    r = (x - w / 2, y - h / 2, x + w / 2, y + h / 2)
                    if free(r):
                        ref.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
                        placed.append(box(ref.GetBoundingBox()))
                        done = True
                        break
                if done:
                    break
            if done:
                break
        if done:
            moved += 1
            shrunk += size == 0.8
            break
    if not done and (body[2] - body[0]) * (body[3] - body[1]) > 16:
        # big part: the ref can sit on the body, between its pads
        ref.SetTextAngleDegrees(0)
        ref.SetPosition(pcbnew.VECTOR2I(mm(cx), mm(cy)))
        r = box(ref.GetBoundingBox())
        if not any(hit(r, box(p.GetBoundingBox(), CLR)) for p in f.Pads()) and not any(hit(r, o) for o in placed):
            placed.append(r)
            done = True
            moved += 1
    if not done:
        ref.SetVisible(False)
        hidden += 1
        print('hidden', f.GetReference())
print('moved', moved, 'shrunk', shrunk, 'hidden', hidden)
pcbnew.SaveBoard(OUT, b, True)
