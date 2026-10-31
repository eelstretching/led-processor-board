#!/usr/bin/env python3
"""Turn a black-on-white SVG into a silkscreen logo footprint (filled F.SilkS polygons), the same
kind of footprint KiCad's bitmap2component makes, but from the command line.

usage: svg_logo.py logo.svg out.kicad_mod NAME --height MM [--px-per-mm 40]
Needs cairosvg, opencv-python-headless and KiCad's pcbnew module (for hole fracturing).
"""
import argparse, uuid
import cairosvg, cv2, numpy as np
import pcbnew

ap = argparse.ArgumentParser()
ap.add_argument("svg"); ap.add_argument("out"); ap.add_argument("name")
ap.add_argument("--height", type=float, required=True, help="logo height in mm (ink, not page)")
ap.add_argument("--px-per-mm", type=float, default=40)
a = ap.parse_args()

# render big, crop to the ink, then scale so the ink is --height mm tall
png = cairosvg.svg2png(url=a.svg, output_width=4000, background_color="white")
img = cv2.imdecode(np.frombuffer(png, np.uint8), cv2.IMREAD_GRAYSCALE)
ink = img < 128
ys, xs = np.nonzero(ink)
ink = ink[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
k = a.height * a.px_per_mm / ink.shape[0]
ink = cv2.resize(ink.astype(np.uint8) * 255, None, fx=k, fy=k, interpolation=cv2.INTER_AREA) > 127
h, w = ink.shape

contours, hier = cv2.findContours(ink.astype(np.uint8), cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
nm = pcbnew.FromMM


def chain(c):
    c = cv2.approxPolyDP(c, 0.6, True)[:, 0, :]   # 0.6 px = 15 um at 40 px/mm
    ch = pcbnew.SHAPE_LINE_CHAIN()
    for x, y in c:
        ch.Append(nm(float(x - w / 2) / a.px_per_mm), nm(float(y - h / 2) / a.px_per_mm))
    ch.SetClosed(True)
    return ch


ps = pcbnew.SHAPE_POLY_SET()
for i, c in enumerate(contours):
    if hier[0][i][3] != -1 or len(c) < 3:
        continue   # holes are added with their outline
    o = ps.AddOutline(chain(c))
    j = hier[0][i][2]
    while j != -1:
        if len(contours[j]) >= 3:
            ps.AddHole(chain(contours[j]), o)
        j = hier[0][j][0]
ps.Fracture()   # silkscreen polygons can't have holes: keyhole them like bitmap2component does

out = ['(footprint "%s"' % a.name, '\t(version 20241229)', '\t(generator "svg_logo.py")',
       '\t(layer "F.Cu")',
       '\t(descr "Silkscreen logo, %.1f x %.1f mm, from %s")' % (w / a.px_per_mm, h / a.px_per_mm, a.svg.split("/")[-1]),
       '\t(attr board_only exclude_from_pos_files exclude_from_bom)']
for fld, val, hide in (("Reference", "G***", True), ("Value", a.name, True)):
    out.append('\t(property "%s" "%s" (at 0 0 0) (layer "F.SilkS") (hide yes) (uuid "%s")'
               ' (effects (font (size 1 1) (thickness 0.15))))' % (fld, val, uuid.uuid4()))
for n in range(ps.OutlineCount()):
    ol = ps.Outline(n)
    pts = " ".join("(xy %.4f %.4f)" % (pcbnew.ToMM(ol.CPoint(i).x), pcbnew.ToMM(ol.CPoint(i).y))
                   for i in range(ol.PointCount()))
    out.append('\t(fp_poly (pts %s) (stroke (width 0) (type solid)) (fill yes) (layer "F.SilkS") (uuid "%s"))'
               % (pts, uuid.uuid4()))
out.append(')')
open(a.out, "w").write("\n".join(out) + "\n")
print("%s: %.1f x %.1f mm, %d polygons" % (a.name, w / a.px_per_mm, h / a.px_per_mm, ps.OutlineCount()))
