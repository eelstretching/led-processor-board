#!/usr/bin/env python3
"""Build led-processor-board.kicad_pcb from the rp2350b-rm2-reference layout.

The reference board's core (RP2350B, flash, PSRAM, crystal, regulators, RM2) keeps its placement
and routing. Its micro-B and single-row headers go; USB-C, the RTC, the power input and the four
output shrouds are placed around it on a wider board (placement.py), and the power, USB-C, OUT4,
radio and SWD runs are hand-routed (preroute.py). Freerouting does the rest afterwards (route.sh).

usage: build_pcb.py REF.kicad_pcb netlist.xml OUT.kicad_pcb PROJECT_DIR
"""
import sys, os, math, traceback
import xml.etree.ElementTree as ET
import pcbnew

REF, NETXML, OUT, PROJ = sys.argv[1:5]
STOCK = '/usr/share/kicad/footprints'
mm = pcbnew.FromMM
MM = pcbnew.ToMM


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


b = pcbnew.LoadBoard(REF)

# ---------------------------------------------------------------- netlist
root = ET.parse(NETXML).getroot()
comps = {}
for c in root.find('components'):
    fp = c.findtext('footprint')
    if not fp:
        continue
    props = {p.get('name'): p.get('value') for p in c.findall('property')}
    comps[c.get('ref')] = dict(value=c.findtext('value'), fp=fp,
                               path=c.find('sheetpath').get('tstamps') + c.findtext('tstamps'),
                               sheetname=props.get('Sheetname', ''), sheetfile=props.get('Sheetfile', ''))
pinnet = {}
for n in root.find('nets'):
    for nd in n.findall('node'):
        pinnet[(nd.get('ref'), nd.get('pin'))] = n.get('name')


def net(name):
    n = b.FindNet(name)
    if n is None:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
    return n


# ---------------------------------------------------------------- remove the old J1-J3
removed_pads = []
for r in ('J1', 'J2', 'J3'):
    f = b.FindFootprintByReference(r)
    for p in f.Pads():
        bb = p.GetBoundingBox()
        removed_pads.append((p.GetNetCode(), pcbnew.BOX2I(bb.GetPosition(), bb.GetSize())))
    b.Delete(f)

tracks = list(b.GetTracks())


def is_via(t):
    return t.GetClass() == 'PCB_VIA'


def is_seg(t):
    return t.GetClass() == 'PCB_TRACK'


# GPIO fanout: keep the part of each trace inside the core, clip the rest (it went to the old
# headers). GPIO24 changes side (it now goes to OUT4), so its trace goes entirely.
CLIP = (84.0, 70.0, 116.0, 160.0)
clip_nets = {'/GPIO%d' % i for i in range(0, 40)} | {'/GPIO40_ADC0', '/GPIO41_ADC1', '/GPIO42_ADC2'}
# the bottom-right fanout went to the old header; OUT4 and SWD now sit elsewhere, so it is rerouted
drop_nets = {'/GPIO%d' % i for i in range(24, 32)} | {'/SWD', '/SWCLK'}
# The RM2 bus ran round the outside of the old right-hand header; keep only its escape from the
# QFN and its last bit into the module, and reroute the middle.
rm2_nets = {'/GPIO43_ADC3', '/GPIO44_ADC4', '/GPIO45_ADC5', '/GPIO46_ADC6'}
RM2_BOXES = ((84.0, 70.0, 108.0, 101.0), (92.0, 141.5, 108.0, 160.0))
# Anything left under the new USB-C receptacle belonged to the micro-B.
USB_AREA = (94.3, 69.0, 105.0, 79.3)


def inside(x, y):
    return CLIP[0] <= x <= CLIP[2] and CLIP[1] <= y <= CLIP[3]


def clip_seg(x0, y0, x1, y1, CLIP=CLIP):
    # Liang-Barsky
    dx, dy = x1 - x0, y1 - y0
    t0, t1 = 0.0, 1.0
    for p, q in ((-dx, x0 - CLIP[0]), (dx, CLIP[2] - x0), (-dy, y0 - CLIP[1]), (dy, CLIP[3] - y0)):
        if p == 0:
            if q < 0:
                return None
        else:
            r = q / p
            if p < 0:
                t0 = max(t0, r)
            else:
                t1 = min(t1, r)
            if t0 > t1:
                return None
    return (x0 + t0 * dx, y0 + t0 * dy, x0 + t1 * dx, y0 + t1 * dy)


def in_box(x, y, bx):
    return bx[0] <= x <= bx[2] and bx[1] <= y <= bx[3]


doomed = []
for t in tracks:
    n = t.GetNetname()
    if is_via(t):
        pts = [t.GetPosition()]
    else:
        pts = [t.GetStart(), t.GetEnd()]
    if any(in_box(MM(p.x), MM(p.y), USB_AREA) for p in pts):
        doomed.append(t)
        continue
    if n in rm2_nets:
        xy = [(MM(p.x), MM(p.y)) for p in pts]
        box = [bx for bx in RM2_BOXES if any(in_box(x, y, bx) for x, y in xy)]
        if not box:
            doomed.append(t)
        elif not all(in_box(x, y, box[0]) for x, y in xy):
            if not is_seg(t):
                doomed.append(t)
                continue
            c = clip_seg(xy[0][0], xy[0][1], xy[1][0], xy[1][1], box[0])
            if c is None or math.hypot(c[2] - c[0], c[3] - c[1]) < 0.05:
                doomed.append(t)
            else:
                t.SetStart(V(c[0], c[1])); t.SetEnd(V(c[2], c[3]))
        continue
    if n in drop_nets:
        doomed.append(t)
        continue
    if n not in clip_nets:
        continue
    if is_via(t):
        p = t.GetPosition()
        if not inside(MM(p.x), MM(p.y)):
            doomed.append(t)
        continue
    s, e = t.GetStart(), t.GetEnd()
    x0, y0, x1, y1 = MM(s.x), MM(s.y), MM(e.x), MM(e.y)
    if inside(x0, y0) and inside(x1, y1):
        continue
    if not is_seg(t):
        doomed.append(t)
        continue
    c = clip_seg(x0, y0, x1, y1)
    if c is None or math.hypot(c[2] - c[0], c[3] - c[1]) < 0.05:
        doomed.append(t)
        continue
    t.SetStart(V(c[0], c[1]))
    t.SetEnd(V(c[2], c[3]))

# Everything else that ran to J1-J3: walk back from each removed pad and delete the chain until
# it reaches a pad, a junction, or a via that still carries other traces.
dset = set(id(t) for t in doomed)
tracks = [t for t in tracks if id(t) not in dset]
alive = set(id(t) for t in tracks)
TOL = mm(0.01)


def at(a, p):
    return abs(a.x - p.x) <= TOL and abs(a.y - p.y) <= TOL


pads_all = [p for f in b.GetFootprints() for p in f.Pads()]


def kill(t):
    alive.discard(id(t))
    doomed.append(t)


queue = []
for t in tracks:
    if not is_seg(t) or t.GetNetname() in clip_nets or t.GetNetname() in rm2_nets:
        continue
    for end, other in ((t.GetStart(), t.GetEnd()), (t.GetEnd(), t.GetStart())):
        if any(nc == t.GetNetCode() and bb.Contains(end) for nc, bb in removed_pads):
            if id(t) in alive:
                kill(t)
                queue.append((t.GetNetCode(), other, t.GetLayer()))
            break

while queue:
    nc, p, layer = queue.pop()
    if any(pd.GetNetCode() == nc and pd.IsOnLayer(layer) and pd.HitTest(p) for pd in pads_all):
        continue
    segs = [t for t in tracks if id(t) in alive and is_seg(t) and t.GetNetCode() == nc
            and (at(t.GetStart(), p) or at(t.GetEnd(), p))]
    vias = [t for t in tracks if id(t) in alive and is_via(t) and t.GetNetCode() == nc and at(t.GetPosition(), p)]
    if vias:
        if len(segs) <= 1:
            kill(vias[0])
            for s in segs:
                kill(s)
                queue.append((nc, s.GetEnd() if at(s.GetStart(), p) else s.GetStart(), s.GetLayer()))
        continue
    same = [s for s in segs if s.GetLayer() == layer]
    if len(same) == 1 and len(segs) == 1:
        s = same[0]
        kill(s)
        queue.append((nc, s.GetEnd() if at(s.GetStart(), p) else s.GetStart(), layer))

for t in doomed:
    b.Delete(t)
del tracks, doomed

# ---------------------------------------------------------------- zones and outline
X0, Y0, X1, Y1 = 50.0, 70.0, 150.0, 159.5


def zbox(z):
    bb = z.GetBoundingBox()
    return (round(MM(bb.GetX()), 1), round(MM(bb.GetY()), 1), round(MM(bb.GetWidth()), 1), round(MM(bb.GetHeight()), 1))


def set_rect(z, x0, y0, x1, y1):
    o = z.Outline()
    o.RemoveAllContours()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(mm(x), mm(y))


for z in list(b.Zones()):
    bx = zbox(z)
    n = z.GetNetname()
    lay = [b.GetLayerName(l) for l in z.GetLayerSet().Seq()]
    if z.GetIsRuleArea() and (bx[0] in (121.4, 78.4)):
        b.Delete(z)                         # keepouts around the old header traces
    elif n in ('VSYS', 'VBUS') and not z.GetIsRuleArea():
        b.Delete(z)                         # micro-B era VBUS/VSYS copper; routed as tracks now
    elif n == '+3V3' and z.GetZoneName() == 'RM2 end 3V3':
        set_rect(z, 83.5, 140.0, 116.5, 160.0)
    elif n == 'GND' and z.GetZoneName() == 'RM2 end GND':
        set_rect(z, 83.5, 140.0, 116.5, 160.0)
    elif n == '+3V3' and bx[2] > 50:        # Pi's top 3V3 pour: keep it to the core
        set_rect(z, 83.5, 70.5, 116.5, 141.0)
    elif n == 'GND' and lay == ['B.Cu'] and bx[2] > 50:
        set_rect(z, X0 + 0.5, Y0 + 0.5, X1 - 0.5, Y1 - 0.5)
    elif z.GetIsRuleArea() and z.GetZoneName().startswith('RM2 antenna'):
        set_rect(z, X0 - 0.5, 154.3, X1 + 0.5, Y1 + 0.5)

# top GND pours left and right of the core (the core keeps Pi's copper)
ref_gnd = [z for z in b.Zones() if z.GetNetname() == 'GND' and not z.GetIsRuleArea()][0]
for side, (xa, xb) in (('left', (X0 + 0.5, 83.5)), ('right', (116.5, X1 - 0.5))):
    gz = pcbnew.ZONE(b)
    gz.SetLayer(pcbnew.F_Cu)
    gz.SetNetCode(net('GND').GetNetCode())
    gz.SetAssignedPriority(0)
    gz.SetZoneName('top GND ' + side)
    set_rect(gz, xa, Y0 + 0.5, xb, Y1 - 0.5)
    gz.SetLocalClearance(ref_gnd.GetLocalClearance())
    gz.SetMinThickness(ref_gnd.GetMinThickness())
    gz.SetPadConnection(ref_gnd.GetPadConnection())
    gz.SetThermalReliefGap(ref_gnd.GetThermalReliefGap())
    gz.SetThermalReliefSpokeWidth(ref_gnd.GetThermalReliefSpokeWidth())
    b.Add(gz)

for d in list(b.GetDrawings()):
    if d.GetLayer() == pcbnew.Edge_Cuts:
        b.Delete(d)
R = 3.0


def edge_line(x0, y0, x1, y1):
    s = pcbnew.PCB_SHAPE(b, pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    s.SetStart(V(x0, y0)); s.SetEnd(V(x1, y1)); b.Add(s)


def edge_arc(cx, cy, sx, sy, ang):
    s = pcbnew.PCB_SHAPE(b, pcbnew.SHAPE_T_ARC)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    s.SetCenter(V(cx, cy)); s.SetStart(V(sx, sy)); s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T), True)
    b.Add(s)


edge_line(X0 + R, Y0, X1 - R, Y0); edge_line(X1, Y0 + R, X1, Y1 - R)
edge_line(X1 - R, Y1, X0 + R, Y1); edge_line(X0, Y1 - R, X0, Y0 + R)
edge_arc(X1 - R, Y0 + R, X1 - R, Y0, 90); edge_arc(X1 - R, Y1 - R, X1, Y1 - R, 90)
edge_arc(X0 + R, Y1 - R, X0 + R, Y1, 90); edge_arc(X0 + R, Y0 + R, X0, Y0 + R, 90)

# ---------------------------------------------------------------- footprints
for ref, c in comps.items():
    f = b.FindFootprintByReference(ref)
    if f is None:
        lib, name = c['fp'].split(':')
        path = os.path.join(PROJ, lib + '.pretty') if lib == 'rp2350b_rm2' else os.path.join(STOCK, lib + '.pretty')
        f = pcbnew.FootprintLoad(path, name)
        f.SetFPID(pcbnew.LIB_ID(lib, name))
        f.SetReference(ref)
        b.Add(f)
    f.SetValue(c['value'])
    f.SetPath(pcbnew.KIID_PATH(c['path']))
    f.SetSheetname(c['sheetname'])
    f.SetSheetfile(c['sheetfile'])
    for p in f.Pads():
        nm = pinnet.get((ref, p.GetNumber()))
        if nm:
            p.SetNet(net(nm))
        elif p.GetNumber():
            p.SetNetCode(0)


def place(ref, x, y, rot=0, ref_at=None):
    f = b.FindFootprintByReference(ref)
    f.SetOrientationDegrees(0)
    f.SetPosition(V(x, y))
    f.SetOrientationDegrees(rot)
    return f


HERE = os.path.dirname(os.path.abspath(__file__))
exec(open(os.path.join(HERE, 'placement.py')).read())
exec(open(os.path.join(HERE, 'preroute.py')).read())

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(OUT, b, True)
print('saved', OUT)
