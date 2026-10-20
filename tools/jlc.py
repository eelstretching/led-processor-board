"""Make the JLCPCB upload set in production/: gerbers + drill zip, and a BOM and
placement (CPL) file for the SMD parts JLC assembles. Everything without an
LCSC number, plus the through-hole parts (BT1, J2-J9), is left off both files
for hand-soldering. Run from the repo root: python3 tools/jlc.py"""
import csv, glob, os, shutil, subprocess, tempfile, zipfile
import pcbnew

BOARD = 'led-processor-board.kicad_pcb'
OUT = 'production'
NOT_JLC = {'U5'}  # RM2: castellated SMD, but bought from Raspberry Pi and hand-soldered
LAYERS = 'F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts'

def cli(*a):
    subprocess.run(['kicad-cli', 'pcb', 'export', *a], check=True, stdout=subprocess.DEVNULL)

os.makedirs(OUT, exist_ok=True)
b = pcbnew.LoadBoard(BOARD)
smd = {f.GetReference() for f in b.GetFootprints()
       if f.GetAttributes() & pcbnew.FP_SMD and f.GetLayer() == pcbnew.F_Cu}

placed, hand = [], []
rows = list(csv.DictReader(open(os.path.join(OUT, 'bom-full.csv'))))
with open(os.path.join(OUT, 'jlcpcb-bom.csv'), 'w', newline='') as fh:
    w = csv.writer(fh)
    w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
    for r in rows:
        refs = r['Designator'].split(',')
        if r['LCSC'] and not set(refs) & NOT_JLC and set(refs) <= smd:
            w.writerow([r['Comment'], r['Designator'], r['Footprint'], r['LCSC']])
            placed += refs
        else:
            hand += refs

with tempfile.TemporaryDirectory() as t:
    pos = os.path.join(t, 'pos.csv')
    cli('pos', '--format', 'csv', '--units', 'mm', '--side', 'front', '--exclude-dnp', '-o', pos, BOARD)
    got = {r['Ref']: r for r in csv.DictReader(open(pos))}
    missing = set(placed) - set(got)
    assert not missing, 'not in the position file: %s' % sorted(missing)
    key = lambda d: (d.rstrip('0123456789'), int(d[len(d.rstrip('0123456789')):]))
    with open(os.path.join(OUT, 'jlcpcb-cpl.csv'), 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        for d in sorted(placed, key=key):
            r = got[d]
            w.writerow([d, r['PosX'] + 'mm', r['PosY'] + 'mm', 'Top', r['Rot']])

    g = os.path.join(t, 'gerbers')
    cli('gerbers', '--layers', LAYERS, '--check-zones', '-o', g + '/', BOARD)
    cli('drill', '--excellon-separate-th', '-o', g + '/', BOARD)
    z = os.path.join(OUT, 'led-processor-board-gerbers.zip')
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(glob.glob(g + '/*')):
            zf.write(f, os.path.basename(f))

print('assembled by JLC: %d parts' % len(placed))
print('hand-solder:', ', '.join(sorted(hand, key=key)))
