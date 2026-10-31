#!/usr/bin/env python3
"""v1.1 silkscreen pass: put every reference designator right next to its own part.

silk.py (v1) only moved a label when its old spot was blocked, so many labels stayed where the
reference layout had them, a few mm from their part and often nearer to a neighbour. This one
re-places every visible F.Silkscreen reference from scratch:

- hard rules: no overlap with pads/holes (+0.15 mm), other silk (+0.15 mm), other labels (+0.2 mm),
  or the board edge (+0.4 mm);
- cost: gap to its own part's courtyard, plus a big penalty when the label is nearer (or nearly as
  near) to another part, plus a small one for sitting on any part body or being rotated;
- most-constrained label first, so the dense core around U1 gets first pick.

Refs in FIXED are placed by hand on one side of their part (L1, the hand-soldered core inductor,
gets a bigger label, and its pin-1 dot is enlarged). Hidden refs in TRY_SHOW are shown if a good
spot exists. Board text is left alone apart from the revision.

usage: silk_near.py in.kicad_pcb out.kicad_pcb
"""
import sys
import numpy as np
import pcbnew

IN, OUT = sys.argv[1:3]
mm, M = pcbnew.FromMM, pcbnew.ToMM
RES = 0.05                     # occupancy grid, mm per cell
PAD_CLR, SILK_CLR, TEXT_CLR, EDGE_CLR = 0.15, 0.15, 0.2, 0.4
b = pcbnew.LoadBoard(IN)

# ref: (side of the part's courtyard, rotation, text size[, x nudge]); the label sits SIDE_GAP off that side
SIDE_GAP = 0.25
FIXED = {'L1': ('N', 0, 1.0),
         # the C12/R7/R8 column trio left of L1 is on a 1 mm pitch: R7 above, R8 below
         'R7': ('N', 90, 0.8), 'R8': ('S', 90, 0.8), 'R2': ('N', 90, 0.8),
         # buck feedback row and the output caps
         'R22': ('N', 90, 0.8), 'R21': ('N', 90, 0.8), 'C31': ('N', 90, 0.8),
         'C27': ('S', 90, 0.8, 0.25), 'C28': ('S', 90, 0.8),   # C27 nudged clear of L2's outline
         'J5': ('W', 90, 1.0), 'J6': ('W', 90, 1.0)}
# series resistors over J7/J8: OUT3's labels below (above is OUT2's shroud), OUT4's above (below is C42)
FIXED.update({'R%d' % n: ('S', 90, 1.0) for n in range(40, 48)})
FIXED.update({'R%d' % n: ('N', 90, 1.0) for n in range(48, 56)})
TRY_SHOW = {'D1', 'R14', 'R15', 'R16', 'C23', 'C24', 'C25'}
# L1's polarity dot: midway up pad 1, in the gap to R8 (footprint coords, mm)
L1_DOT = ((-1.4, 0.0), 0.17)
REV = ('LED processor board  rev A', 'LED processor board  v1.1')
SKIP = {'MH1', 'MH2', 'MH3', 'MH4', 'U5'}   # mounting holes; RM2 keeps its ref on the module outline
# JLC-placed caps with no spot that reads clearly: C6/C7 are boxed in between L1, R3/C9 and U1,
# C29 between U7, C27, the R22/R21 feedback pair and L2's pad
HIDE = {'C6', 'C7', 'C29'}

eb = b.GetBoardEdgesBoundingBox()
X0, Y0 = M(eb.GetLeft()), M(eb.GetTop())
W, H = int((M(eb.GetRight()) - X0) / RES) + 1, int((M(eb.GetBottom()) - Y0) / RES) + 1
occ = np.zeros((H, W), np.uint8)


def box(bb, g=0.0):
    return (M(bb.GetLeft()) - g, M(bb.GetTop()) - g, M(bb.GetRight()) + g, M(bb.GetBottom()) + g)


def cells(r):
    return (max(int((r[0] - X0) / RES), 0), max(int((r[1] - Y0) / RES), 0),
            min(int(np.ceil((r[2] - X0) / RES)), W), min(int(np.ceil((r[3] - Y0) / RES)), H))


def mark(r):
    c0, r0, c1, r1 = cells(r)
    occ[r0:r1, c0:c1] = 1


def mark_seg(a, z, w):
    # thick line as a run of small squares, so diagonal silk doesn't block its whole bbox
    n = max(int(np.hypot(z[0] - a[0], z[1] - a[1]) / RES), 1)
    for i in range(n + 1):
        x, y = a[0] + (z[0] - a[0]) * i / n, a[1] + (z[1] - a[1]) * i / n
        mark((x - w, y - w, x + w, y + w))


def mark_item(g, clr):
    if g.GetClass() in ('PCB_SHAPE', 'FP_SHAPE') and g.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        s, e = g.GetStart(), g.GetEnd()
        mark_seg((M(s.x), M(s.y)), (M(e.x), M(e.y)), M(g.GetWidth()) / 2 + clr)
    else:
        mark(box(g.GetBoundingBox(), clr))


# edge margin
occ[:, :int(EDGE_CLR / RES)] = 1; occ[:, W - int(EDGE_CLR / RES):] = 1
occ[:int(EDGE_CLR / RES), :] = 1; occ[H - int(EDGE_CLR / RES):, :] = 1

foots = {f.GetReference(): f for f in b.GetFootprints()}
court = {}
for ref, f in foots.items():
    cy = f.GetCourtyard(pcbnew.F_CrtYd)
    court[ref] = box(cy.BBox()) if cy.OutlineCount() else box(f.GetBoundingBox(False))
    for p in f.Pads():
        if p.IsOnLayer(pcbnew.F_Mask) or p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
            mark(box(p.GetBoundingBox(), PAD_CLR))
    for g in f.GraphicalItems():
        if g.GetLayer() == pcbnew.F_SilkS and g.GetClass() != 'PCB_FIELD':
            mark_item(g, SILK_CLR)
    for fld in f.GetFields():   # visible silk fields other than the reference (none expected)
        if fld.GetLayer() == pcbnew.F_SilkS and fld.IsVisible() and not fld.IsReference():
            mark(box(fld.GetBoundingBox(), SILK_CLR))
for d in b.GetDrawings():
    if d.GetLayer() == pcbnew.F_SilkS:
        mark_item(d, SILK_CLR)
for t in b.GetTracks():
    if t.GetClass() == 'PCB_VIA' and not t.IsTented(pcbnew.F_Mask):
        mark(box(t.GetBoundingBox(), PAD_CLR))


def gap(a, c):
    dx = max(c[0] - a[2], a[0] - c[2], 0)
    dy = max(c[1] - a[3], a[1] - c[3], 0)
    return np.hypot(dx, dy)


def overlap(a, c):
    return max(min(a[2], c[2]) - max(a[0], c[0]), 0) * max(min(a[3], c[3]) - max(a[1], c[1]), 0)


def text_size(ref):
    t = foots[ref].Reference()
    return M(t.GetTextHeight())


def candidates(ref, size):
    """All feasible spots for this ref at this text size: array of rows
    (cost, anchor x, anchor y, rot, x0, y0, x1, y1), where x0..y1 is the text box plus clearance."""
    t = foots[ref].Reference()
    own = court[ref]
    near = [court[r] for r in court if r != ref and r not in SKIP and gap(court[r], own) < 6]
    ii = np.pad(occ.cumsum(0).cumsum(1), ((1, 0), (1, 0))).astype(np.int64)
    cx, cy = (own[0] + own[2]) / 2, (own[1] + own[3]) / 2
    rows = []
    for rot in (0, 90):
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.15))
        t.SetTextAngleDegrees(rot)
        t.SetPosition(pcbnew.VECTOR2I(0, 0))
        tb = box(t.GetBoundingBox())
        w, h = tb[2] - tb[0], tb[3] - tb[1]
        ox, oy = (tb[0] + tb[2]) / 2, (tb[1] + tb[3]) / 2     # bbox centre vs anchor
        reach = 3.5
        X, Y = np.meshgrid(np.arange(own[0] - reach - w / 2, own[2] + reach + w / 2, 0.1),
                           np.arange(own[1] - reach - h / 2, own[3] + reach + h / 2, 0.1))
        X, Y = X.ravel(), Y.ravel()
        x0, y0, x1, y1 = X - w / 2, Y - h / 2, X + w / 2, Y + h / 2
        g = TEXT_CLR / 2
        c0 = np.floor((x0 - g - X0) / RES).astype(int); r0 = np.floor((y0 - g - Y0) / RES).astype(int)
        c1 = np.ceil((x1 + g - X0) / RES).astype(int); r1 = np.ceil((y1 + g - Y0) / RES).astype(int)
        ok = (c0 > 0) & (r0 > 0) & (c1 < W) & (r1 < H)
        c0, r0, c1, r1 = (np.clip(v, 0, lim) for v, lim in ((c0, W), (r0, H), (c1, W), (r1, H)))
        ok &= (ii[r1, c1] - ii[r0, c1] - ii[r1, c0] + ii[r0, c0]) == 0

        def vgap(c):
            dx = np.maximum(np.maximum(c[0] - x1, x0 - c[2]), 0)
            dy = np.maximum(np.maximum(c[1] - y1, y0 - c[3]), 0)
            return np.hypot(dx, dy)

        def vover(c):
            return (np.maximum(np.minimum(x1, c[2]) - np.maximum(x0, c[0]), 0) *
                    np.maximum(np.minimum(y1, c[3]) - np.maximum(y0, c[1]), 0)) / (w * h)

        d = vgap(own)
        cost = d.copy()
        for o in near:
            do = vgap(o)
            amb = do < d + 0.6          # nearer, or nearly as near, to another part
            cost += np.where(amb, 4 + 4 * (d + 0.6 - do), 0)
            cost += 1.5 * vover(o)
        cost += 0.5 * vover(own)
        cost += 0.05 * np.minimum(abs(X - cx), abs(Y - cy))
        cost += 0.25 if rot else 0
        k = ok
        rows.append(np.stack([cost[k], X[k] - ox, Y[k] - oy, np.full(k.sum(), rot),
                              x0[k] - g, y0[k] - g, x1[k] + g, y1[k] + g], 1))
    out = np.concatenate(rows) if rows else np.zeros((0, 8))
    return out[np.argsort(out[:, 0])]


def place(ref, x, y, rot, size):
    t = foots[ref].Reference()
    t.SetVisible(True)
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(0.15))
    t.SetTextAngleDegrees(rot)
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
    t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    t.SetKeepUpright(True)
    t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    r = box(t.GetBoundingBox())
    mark((r[0] - TEXT_CLR / 2, r[1] - TEXT_CLR / 2, r[2] + TEXT_CLR / 2, r[3] + TEXT_CLR / 2))


def place_side(ref, side, rot, size, dx=0.0):
    t = foots[ref].Reference()
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(0.15))
    t.SetTextAngleDegrees(rot)
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    t.SetPosition(pcbnew.VECTOR2I(0, 0))
    tb = box(t.GetBoundingBox())
    w, h = tb[2] - tb[0], tb[3] - tb[1]
    ox, oy = (tb[0] + tb[2]) / 2, (tb[1] + tb[3]) / 2
    c = court[ref]
    x, y = (c[0] + c[2]) / 2, (c[1] + c[3]) / 2
    if side == 'N': y = c[1] - SIDE_GAP - h / 2
    if side == 'S': y = c[3] + SIDE_GAP + h / 2
    if side == 'W': x = c[0] - SIDE_GAP - w / 2
    if side == 'E': x = c[2] + SIDE_GAP + w / 2
    place(ref, x - ox + dx, y - oy, rot, size)


for ref, spec in FIXED.items():
    place_side(ref, *spec)

l1 = foots['L1']
for g in l1.GraphicalItems():
    if g.GetLayer() == pcbnew.F_SilkS and g.GetClass() in ('PCB_SHAPE', 'FP_SHAPE') and g.GetShape() == pcbnew.SHAPE_T_CIRCLE:
        (dx, dy), rad = L1_DOT
        c = l1.GetPosition() + pcbnew.VECTOR2I(mm(dx), mm(dy))
        g.SetCenter(c); g.SetEnd(c + pcbnew.VECTOR2I(mm(rad), 0))
        g.SetWidth(0); g.SetFilled(True)
        mark(box(g.GetBoundingBox(), SILK_CLR))
for d in b.GetDrawings():
    if d.GetClass() == 'PCB_TEXT' and d.GetText() == REV[0]:
        d.SetText(REV[1])

for r in HIDE:
    foots[r].Reference().SetVisible(False)
todo = [r for r, f in foots.items() if r not in FIXED and r not in SKIP and r not in HIDE and f.GetLayer() == pcbnew.F_Cu
        and ((f.Reference().IsVisible() and f.Reference().GetLayer() == pcbnew.F_SilkS) or r in TRY_SHOW)]
# justify every ref centred first, so the bbox maths above is about the text itself
for r in todo:
    t = foots[r].Reference()
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    t.SetVisible(True)

report = []
cands = {}
for r in todo:
    size = text_size(r)
    c = candidates(r, size)
    if not len(c) and size > 0.8:
        size = 0.8
        c = candidates(r, size)
    cands[r] = (c, size)
while todo:
    def key(r):
        c = cands[r][0]
        if not len(c):
            return (10 ** 6, 0)
        return ((c[:, 0] < c[0, 0] + 0.5).sum(), c[0, 0])   # fewest good options first
    r = min(todo, key=key)
    todo.remove(r)
    c, size = cands[r]
    f = foots[r]
    if not len(c) or (r in TRY_SHOW and c[0, 0] > 1.0):
        f.Reference().SetVisible(False)
        report.append((r, 'hidden'))
        continue
    cost, x, y, rot = (float(v) for v in c[0, :4])
    place(r, x, y, int(rot), size)
    report.append((r, round(cost, 2)))
    t = box(f.Reference().GetBoundingBox())
    for o in todo:   # drop the other labels' spots that now collide with this one
        oc, osz = cands[o]
        clash = (oc[:, 4] < t[2] + TEXT_CLR / 2) & (t[0] - TEXT_CLR / 2 < oc[:, 6]) & \
                (oc[:, 5] < t[3] + TEXT_CLR / 2) & (t[1] - TEXT_CLR / 2 < oc[:, 7])
        cands[o] = (oc[~clash], osz)

for r, c in sorted(report, key=lambda k: 1e9 if k[1] == 'hidden' else k[1], reverse=True):
    print(r, c)
pcbnew.SaveBoard(OUT, b, True)
