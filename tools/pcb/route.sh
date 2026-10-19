#!/bin/sh
# route.sh in.kicad_pcb out.kicad_pcb passes jar
set -e
D=$(dirname $0); IN=$1; OUT=$2; MP=${3:-20}; JAR=${4:-/opt/freerouting/fr19.jar}
python3 - $IN $D/r.dsn <<'PY' 2>/dev/null
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1]); print(pcbnew.ExportSpecctraDSN(b, sys.argv[2]))
PY
python3 $D/dsn_prep.py $D/r.dsn $D/r2.dsn
rm -f $D/r2.ses
xvfb-run -a java -jar $JAR -de $D/r2.dsn -do $D/r2.ses -mp $MP -mt 1 > $D/fr.log 2>&1 || true
ls -la $D/r2.ses
python3 - $IN $D/r2.ses $OUT <<'PY' 2>/dev/null
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
print('import', pcbnew.ImportSpecctraSES(b, sys.argv[2]))
b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(sys.argv[3], b, True)
PY
