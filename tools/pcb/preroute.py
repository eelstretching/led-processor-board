# hand routing for the buck (exec'd by build_pcb.py after placement)
def trk(net_name, pts, w, layer=pcbnew.F_Cu):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(V(x0, y0)); t.SetEnd(V(x1, y1))
        t.SetWidth(mm(w)); t.SetLayer(layer)
        t.SetNet(net(net_name))
        b.Add(t)


def via(net_name, x, y, d=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y)); v.SetWidth(mm(d)); v.SetDrill(mm(drill))
    v.SetNet(net(net_name))
    b.Add(v)


# 12 V in: J4 -> D3 -> input caps -> VIN
trk('/Power/VIN_RAW', [(146.5, 90.0), (145.9, 90.6), (141.32, 90.6)], 0.8)
trk('+12V', [(137.32, 90.6), (134.7, 90.45), (132.6, 90.45), (130.65, 90.45)], 0.8)
trk('+12V', [(130.65, 90.45), (129.6, 90.45), (128.95, 90.138)], 0.6)
# input caps' GND straight down to the bottom plane
for x, y in ((130.65, 87.6), (132.6, 87.45), (134.7, 87.45)):
    trk('GND', [(x, y), (x, y + 1.0)], 0.5)
    via('GND', x, y)
# SW node: pin 2 straight down into L2, boot cap alongside
trk('/Power/SW', [(128.0, 90.138), (128.0, 93.0)], 0.8)
trk('/Power/SW', [(125.0, 90.775), (125.0, 93.0)], 0.5)
trk('Net-(U7-BOOT)', [(127.05, 87.862), (126.3, 87.862), (125.0, 89.162), (125.0, 89.225)], 0.3)
# U7 GND: the boot cap and SW wall it in on top, so it drops to the plane
trk('GND', [(127.05, 90.138), (126.5, 91.5)], 0.4)
via('GND', 126.5, 91.5)
# EN pull-up from the 12 V riser; EN hops under the FB parts on B.Cu
trk('+12V', [(136.2, 87.11), (136.6, 88.0), (136.6, 89.9)], 0.3)
trk('Net-(U7-EN)', [(136.2, 86.09), (136.2, 85.2)], 0.25)
via('Net-(U7-EN)', 136.2, 85.2)
trk('Net-(U7-EN)', [(136.2, 85.2), (128.0, 86.3)], 0.25, pcbnew.B_Cu)
via('Net-(U7-EN)', 128.0, 86.3)
trk('Net-(U7-EN)', [(128.0, 86.3), (128.0, 87.862)], 0.25)
# FB divider right above FB
trk('/Power/FB', [(128.95, 87.862), (128.95, 86.6), (129.44, 86.11), (130.0, 86.11), (131.4, 86.11), (132.8, 86.08)], 0.25)
trk('+5V', [(131.4, 85.09), (132.8, 85.12)], 0.25)
trk('GND', [(130.0, 85.09), (129.3, 84.4)], 0.3)
via('GND', 129.3, 84.4)
# output: L2 into the three 22u, their GND to the plane
trk('+5V', [(128.0, 100.4), (128.0, 101.65)], 0.8)
trk('+5V', [(125.5, 101.65), (130.5, 101.65)], 0.8)
for x in (125.5, 128.0, 130.5):
    trk('GND', [(x, 103.55), (x, 104.7)], 0.5)
    via('GND', x, 104.7)

# USB-C, left to right: B5 CC2, A8, B6 D+, A7 D-, A6 D+, B7 D-, A5 CC1, B8, A4 VBUS (0.5 mm pitch).
# A6/B7 carry the pair down to R7/R8; B6 joins D+ round the A7 via, and A7 hops to D- on B.Cu
# (above Pi's GPIO47 trace, which runs across under here on B.Cu).
PY = 77.695
trk('/USB_D+', [(99.55, PY), (99.55, 89.25), (99.725, 89.425)], 0.15)
trk('/USB_D-', [(100.05, PY), (100.05, 79.9), (100.15, 80.3), (100.05, 80.7), (100.05, 88.35), (100.675, 88.975)], 0.15)
trk('/USB_D+', [(98.55, PY), (98.55, 78.7), (98.35, 78.9), (98.35, 79.9), (99.55, 81.1)], 0.15)
trk('/USB_D-', [(99.05, PY), (99.05, 78.42), (98.95, 79.3)], 0.15)
via('/USB_D-', 98.95, 79.3)
trk('/USB_D-', [(98.95, 79.3), (100.15, 80.3)], 0.15, pcbnew.B_Cu)
via('/USB_D-', 100.15, 80.3)
# CC pull-downs
trk('Net-(J1-CC1)', [(100.55, PY), (100.55, 78.42), (101.0, 78.87), (102.0, 79.89)], 0.2)
trk('Net-(J1-CC2)', [(97.55, PY), (97.55, 78.42), (97.1, 78.87), (96.6, 79.89)], 0.2)
for x in (102.0, 96.6):
    trk('GND', [(x, 80.91), (x, 81.8)], 0.3)
    via('GND', x, 81.8)
# VBUS: both VBUS pads up past the locating pegs, across under the receptacle, then to D2;
# D2/D4 feed VSYS at C1 and U2
trk('VBUS', [(96.85, PY), (96.95, 77.0), (97.25, 76.45), (97.25, 75.3), (101.35, 75.3)], 0.3)
trk('VBUS', [(101.75, PY), (101.65, 77.0), (101.35, 76.45), (101.35, 75.3)], 0.3)
trk('VBUS', [(101.35, 75.3), (104.6, 75.3), (105.85, 74.05)], 0.4)
trk('VSYS', [(109.15, 74.0), (111.15, 74.0)], 0.5)
trk('VSYS', [(109.15, 74.0), (108.2, 74.95), (107.04, 75.6)], 0.5)
trk('VSYS', [(107.04, 76.9), (105.7, 78.2)], 0.5)

# ---------------------------------------------------------------- OUT4 bus (GPIO24-31) and SWD
# Pi's escapes off the bottom of U1, then one 0.3 mm-pitch bus down a channel right of SW2
# and across to R48-R55. GPIO24-27 escape left and U-turn under X1; GPIO28-31 escape right.
C = 0.5                                   # 45-degree chamfer
CH = {k: 112.75 + 0.3 * (k - 24) for k in range(24, 32)}      # channel x
HB = {k: 133.5 + 0.3 * (31 - k) for k in range(24, 32)}       # bottom run y (GPIO31 on top)
HL = {27: 118.0, 26: 118.3, 25: 118.6, 24: 118.9}             # left group's crossing y
RX = {k: 121.0 + 2.54 * (k - 24) for k in range(24, 32)}      # R48-R55 x
RY = 139.19                                                   # R48-R55 pad 2 y
LEFT = {24: ((97.8, 105.9), 93.8), 25: ((98.2, 106.0), 94.6),
        26: ((98.6, 106.1), 95.4), 27: ((99.0, 106.2), 96.2)}
for k, ((px, py), vx) in LEFT.items():
    dy = py + (px - vx)                   # 45-degree run down to the vertical
    trk('/GPIO%d' % k, [(px, 104.9), (px, py), (vx, dy), (vx, HL[k] - C), (vx + C, HL[k]),
                        (CH[k] - C, HL[k]), (CH[k], HL[k] + C)], 0.15)
RIGHT = {28: [(102.2, 105.9), (104.1, 107.8), (109.8, 107.8)],
         29: [(102.6, 105.79), (104.21, 107.4), (110.6, 107.4)],
         30: [(103.0, 105.7), (104.3, 107.0), (111.41, 107.0)],
         31: [(103.4, 105.6), (104.4, 106.6), (113.55, 106.6)]}
for k, pts in RIGHT.items():
    ex, ey = pts[-1]
    trk('/GPIO%d' % k, [(pts[0][0], 104.9)] + pts + [(CH[k], ey + CH[k] - ex)], 0.15)
for k in range(24, 32):
    top = HL[k] + C if k in HL else RIGHT[k][-1][1] + CH[k] - RIGHT[k][-1][0]
    trk('/GPIO%d' % k, [(CH[k], top), (CH[k], HB[k] - C), (CH[k] + C, HB[k]),
                        (RX[k] - C, HB[k]), (RX[k], HB[k] + C), (RX[k], RY)], 0.15)
# SWD header J3 (vertical, pin 1 = SWCLK at the top) right under the escapes
trk('/SWCLK', [(101.0, 104.9), (101.0, 106.3), (104.7, 110.0), (104.7, 111.8)], 0.15)
trk('/SWD', [(101.4, 104.9), (101.4, 106.19), (106.2, 110.99), (106.2, 115.9), (105.22, 116.88), (104.7, 116.88)], 0.15)

# ---------------------------------------------------------------- RM2 bus (GPIO43-46)
# Pi's escapes run up-right off U1; drop each to B.Cu on its diagonal, run down the right of
# the core and come back in to the RM2 (U5) pads and Pi's R14/R15 on GPIO45.
RM2 = {43: [(108.0, 98.0), (111.8, 98.0), (112.6, 97.2)],
       44: [(108.0, 97.6), (111.0, 97.6), (113.6, 95.0)],
       45: [(108.0, 97.2), (110.19, 97.2), (114.6, 92.79)],
       46: [(108.0, 96.8), (109.4, 96.8), (115.6, 90.6)]}
NM = {43: '/GPIO43_ADC3', 44: '/GPIO44_ADC4', 45: '/GPIO45_ADC5', 46: '/GPIO46_ADC6'}
for k, pts in RM2.items():
    trk(NM[k], pts, 0.15)
    via(NM[k], *pts[-1])
B = pcbnew.B_Cu
trk(NM[43], [(112.6, 97.2), (112.6, 136.0), (111.6, 137.0), (99.5, 137.0), (98.5, 138.0), (98.5, 140.6)], 0.15, B)
via(NM[43], 98.5, 140.6)
trk(NM[43], [(98.5, 140.6), (98.5, 141.5)], 0.15)
trk(NM[44], [(113.6, 95.0), (113.6, 137.0), (112.6, 138.0), (104.0, 138.0), (103.0, 139.0), (103.0, 140.3)], 0.15, B)
via(NM[44], 103.0, 140.3)
trk(NM[44], [(103.0, 140.3), (103.0, 141.5)], 0.15)
trk(NM[45], [(114.6, 92.79), (114.6, 146.45), (113.5, 147.55)], 0.15, B)
via(NM[45], 113.5, 147.55)
trk(NM[45], [(113.5, 147.55), (108.0, 147.55)], 0.15)
trk(NM[45], [(109.81, 147.55), (109.81, 146.05)], 0.15)
trk(NM[45], [(112.0, 147.55), (112.0, 146.91)], 0.15)
trk(NM[46], [(115.6, 90.6), (115.6, 147.6), (112.65, 150.55), (109.0, 150.55)], 0.15, B)
via(NM[46], 109.0, 150.55)
trk(NM[46], [(109.0, 150.55), (108.0, 150.55)], 0.15)

# ---------------------------------------------------------------- +3V3 the router missed
# J7 pin 13: between the shroud rows on B.Cu to the RM2-end 3V3 pour
trk('+3V3', [(78.24, 147.2), (79.51, 145.93), (84.6, 145.93)], 0.3, B)
via('+3V3', 84.6, 145.93)
# RTC side (R17-R19, U6) is walled off from the core pour by the OUT4 and RM2 buses:
# feed it from J2 pin 1 on B.Cu
trk('+3V3', [(121.29, 115.67), (120.4, 115.0)], 0.3)
via('+3V3', 120.4, 115.0)
trk('+3V3', [(120.4, 115.0), (120.4, 86.0), (118.5, 86.0)], 0.3, B)
trk('+3V3', [(118.5, 86.0), (116.0, 86.0)], 0.3)
