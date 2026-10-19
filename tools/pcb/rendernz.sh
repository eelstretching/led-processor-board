#!/bin/sh
# like render.sh but with zone fills removed so tracks are readable
T=$(mktemp -d)
python3 - "$1" $T/b.kicad_pcb <<'PY' 2>/dev/null
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
for z in b.Zones():
    z.UnFill()
pcbnew.SaveBoard(sys.argv[2], b, True)
PY
shift
$(dirname $0)/render.sh $T/b.kicad_pcb "$@"
rm -rf $T
