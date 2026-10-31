#!/usr/bin/env python3
"""Render a fab-style silkscreen preview PNG: green mask, gold pads, white silk, faint part bodies
(F.Fab) so you can see which label belongs to which part. Needs kicad-cli, cairosvg and Pillow.

usage: silk_preview.py board.kicad_pcb out.png [--px-per-mm N] [--crop x0,y0,x1,y1]
  --crop is in board mm (the coordinates KiCad shows), e.g. --crop 94,84,112,98 for L1
"""
import argparse, os, re, subprocess, tempfile
import cairosvg
import pcbnew
from PIL import Image, ImageChops

LAYERS = ["Edge.Cuts", "F.Mask", "F.Fab", "F.SilkS"]


def export(pcb, layers, svg, bw=True):
    # --page-size-mode 2: the page is the board outline's bounding box
    subprocess.run(["kicad-cli", "pcb", "export", "svg", "--mode-single", "--page-size-mode", "2",
                    "--exclude-drawing-sheet", "-l", layers, "-o", svg, pcb]
                   + (["--black-and-white"] if bw else []), check=True, capture_output=True)


def layer_mask(pcb, layer, tmp, width):
    svg, png = os.path.join(tmp, layer + ".svg"), os.path.join(tmp, layer + ".png")
    export(pcb, layer + ",Edge.Cuts", svg)
    cairosvg.svg2png(url=svg, write_to=png, output_width=width, background_color="white")
    return ImageChops.invert(Image.open(png).convert("L"))   # ink = 255


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcb"); ap.add_argument("out")
    ap.add_argument("--px-per-mm", type=float, default=24)
    ap.add_argument("--crop", help="x0,y0,x1,y1 in board mm")
    a = ap.parse_args()
    bb = pcbnew.LoadBoard(a.pcb).GetBoardEdgesBoundingBox()
    ox, oy = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())
    with tempfile.TemporaryDirectory() as tmp:
        svg = os.path.join(tmp, "e.svg")
        export(a.pcb, "Edge.Cuts", svg)
        wmm = float(re.search(r'width="([\d.]+)mm"', open(svg).read(2000)).group(1))
        m = {l: layer_mask(a.pcb, l, tmp, int(wmm * a.px_per_mm)) for l in LAYERS}
    size = m["F.SilkS"].size
    img = Image.new("RGB", size, (24, 92, 52))                           # solder mask
    img.paste((150, 160, 150), mask=m["F.Fab"].point(lambda v: v // 3))  # faint bodies
    img.paste((205, 170, 90), mask=m["F.Mask"])                          # exposed pads
    img.paste((250, 250, 250), mask=m["F.SilkS"])                        # silkscreen
    img.paste((0, 0, 0), mask=m["Edge.Cuts"])
    if a.crop:
        x0, y0, x1, y1 = (float(v) for v in a.crop.split(","))
        k = size[0] / wmm
        img = img.crop((int((x0 - ox) * k), int((y0 - oy) * k), int((x1 - ox) * k), int((y1 - oy) * k)))
    img.save(a.out)


if __name__ == "__main__":
    main()
