#!/bin/sh
# render.sh board.kicad_pcb out.png [x y w h (mm)] [px-per-mm]
B=$1; O=$2; T=$(mktemp -d)
kicad-cli pcb export svg --mode-single -l ${LAYERS:-B.Cu,F.Cu,F.Silkscreen,Edge.Cuts} --page-size-mode 1 --exclude-drawing-sheet -o $T/b.svg $B >/dev/null 2>&1 || kicad-cli pcb export svg --mode-single -l ${LAYERS:-B.Cu,F.Cu,F.Silkscreen,Edge.Cuts} -o $T/b.svg $B
python3 - $T/b.svg $O "$3" "$4" "$5" "$6" "${7:-20}" <<'PY'
import sys, cairosvg, re
svg,out,x,y,w,h,k=sys.argv[1:]
s=open(svg).read()
if x:
    # rewrite the viewBox to the crop window (svg units are mm when --page-size-mode is board)
    m=re.search(r'viewBox="([-\d. ]+)"',s); vb=[float(v) for v in m.group(1).split()]
    W=re.search(r'width="([\d.]+)(\w*)"',s); 
    s=s.replace(m.group(0),'viewBox="%s %s %s %s"'%(x,y,w,h),1)
    s=re.sub(r'width="[\d.]+\w*"','width="%smm"'%w,s,1); s=re.sub(r'height="[\d.]+\w*"','height="%smm"'%h,s,1)
    pw=int(float(w)*float(k))
else:
    W=re.search(r'width="([\d.]+)(\w*)"',s); pw=int(float(W.group(1))*float(k))
cairosvg.svg2png(bytestring=s.encode(),write_to=out,output_width=pw,background_color='#101010')
PY
rm -rf $T
