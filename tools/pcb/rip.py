#!/usr/bin/env python3
"""Rip up the routed J2/RTC fan-out (GPIO32-42) outside the core escapes so it can be rerouted.

usage: rip.py in.kicad_pcb out.kicad_pcb
"""
import sys
import pcbnew

IN, OUT = sys.argv[1:3]
M = pcbnew.ToMM
b = pcbnew.LoadBoard(IN)
NETS = {'/GPIO%d' % i for i in range(32, 40)} | {'/GPIO40_ADC0', '/GPIO41_ADC1', '/GPIO42_ADC2'}
doomed = []
for t in b.GetTracks():
    if t.GetNetname() not in NETS:
        continue
    pts = [t.GetPosition()] if t.GetClass() == 'PCB_VIA' else [t.GetStart(), t.GetEnd()]
    if t.GetClass() == 'PCB_VIA' or any(M(p.x) > 108.0 for p in pts):
        doomed.append(t)
for t in doomed:
    b.Delete(t)
print('ripped', len(doomed))
pcbnew.SaveBoard(OUT, b, True)
