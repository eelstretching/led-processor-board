# placement of the new parts (exec'd by build_pcb.py; coordinates in mm, KiCad page coords)
P = 2.54

# output shrouds: J5/J6 on the left edge, J7/J8 along the bottom; data (even) row faces the core
place('J5', 55.5, 83.2, 0)
place('J6', 55.5, 115.0, 0)
place('J7', 63.0, 147.2, 90)
place('J8', 121.0, 147.2, 90)
for k in range(8):
    place('R%d' % (24 + k), 65.0, 83.2 + P * k, 0)
    place('R%d' % (32 + k), 65.0, 115.0 + P * k, 0)
    place('R%d' % (40 + k), 63.0 + P * k, 139.7, 90)
    place('R%d' % (48 + k), 121.0 + P * k, 139.7, 90)
place('C36', 55.5, 108.0); place('C37', 58.3, 108.0)
place('C38', 55.5, 139.8); place('C39', 58.3, 139.8)
place('C40', 78.24, 153.0); place('C41', 80.78, 153.0)
place('C42', 136.24, 153.0); place('C43', 138.78, 153.0)

place('MH1', 54.0, 74.0); place('MH2', 146.0, 74.0)
place('MH3', 146.0, 156.0); place('MH4', 54.0, 156.0)

# USB-C in the micro-B's place; D2 moves right to make room
place('J1', 99.3, 73.65, 180)
place('R56', 102.0, 80.4, 270)
place('R57', 96.6, 80.4, 270)
place('D2', 107.5, 74.0, 180)
place('D4', 112.8, 74.0, 0)

# spare and SWD headers
place('J2', 118.5, 86.0, 0)
place('J3', 104.7, 111.8, 0)

# 12 V in and the buck
place('J4', 146.5, 90.0, 90)
place('D3', 139.32, 90.6, 0)
place('C28', 134.7, 89.5, 90)
place('C27', 132.6, 89.5, 90)
place('C29', 130.65, 89.5, 90)
place('U7', 128.0, 89.0, 90)
place('C30', 125.0, 90.0, 270)
place('L2', 128.0, 96.5, 270)
place('R20', 136.2, 86.6, 90)
place('R22', 130.0, 85.6, 90)
place('R21', 131.4, 85.6, 270)
place('C31', 132.8, 85.6, 270)
place('C32', 125.5, 102.6, 270)
place('C33', 128.0, 102.6, 270)
place('C34', 130.5, 102.6, 270)
place('R23', 134.5, 98.0, 270)
place('D5', 134.5, 101.0, 90)

# RTC
place('U6', 130.0, 112.5, 180)
place('C26', 137.3, 115.67, 0)
place('R17', 121.8, 115.67, 0)
place('R18', 121.8, 116.95, 0)
place('R19', 137.3, 113.6, 0)
place('BT1', 131.0, 126.8, 0)

# the old header labels go; Pi's design credit moves to the free space bottom-left of the core
for d in list(b.GetDrawings()):
    if d.GetClass() != 'PCB_TEXT':
        continue
    x = round(MM(d.GetPosition().x), 3)
    if x in (80.315, 119.685) or d.GetText() == 'ADC 40-42':
        b.Delete(d)
    elif d.GetText().startswith(('Raspberry Pi RP2350B', 'Minimal Design Example')):
        b.Delete(d)


def silk(txt, x, y, h=1.0, rot=0, just=0):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(txt)
    t.SetLayer(pcbnew.F_SilkS)
    t.SetPosition(V(x, y))
    t.SetTextSize(pcbnew.VECTOR2I(mm(h), mm(h)))
    t.SetTextThickness(mm(h * 0.15))
    t.SetTextAngleDegrees(rot)
    if just:
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT if just < 0 else pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    b.Add(t)


silk('OUT1  GP0-7', 62.9, 92.1, 1.0, 90)
silk('OUT2  GP8-15', 62.9, 123.9, 1.0, 90)
silk('OUT3  GP16-23', 69.0, 153.3)
silk('OUT4  GP24-31', 127.0, 153.3)
silk('12V IN', 146.5, 81.6)
silk('GND', 143.6, 86.5, 0.8)
silk('+12V', 143.4, 90.0, 0.8)
for i, lab in enumerate(['3V3'] + ['GP%d' % g for g in range(35, 43)] + ['GND']):
    silk(lab, 119.9, 86.0 + P * i, 0.8, 0, -1)
silk('CLK', 103.5, 111.8, 0.8, 0, 1); silk('GND', 103.5, 114.34, 0.8, 0, 1); silk('DIO', 103.5, 116.88, 0.8, 0, 1)
silk('LED processor board  rev A', 99.0, 124.0, 1.2)
silk('core: Raspberry Pi RP2350B minimal design R4', 99.0, 126.0, 0.8)
