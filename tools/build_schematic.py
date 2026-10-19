"""Build the LED processor board schematic from the RP2350B-RM2 reference.

Root sheet: the reference (RP2350B, flash, PSRAM, RM2, USB, crystal, buttons) with the
J2/J3 single-row headers replaced by the DS3231 RTC, a spare-GPIO header and an SWD
header. GPIO0-31 become global labels and go to the Outputs sheet (four 16-pin ribbon
shrouds, pinout as the output boards). Power sheet: 12 V in -> TPS54202H buck -> +5 V
(ribbons) -> B5819W -> VSYS (existing NCP1117 3.3 V regulator).
"""
import sys, os, copy, math, uuid as _uuid, json, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sx, libs
from sx import Sym, S

REF_DIR = os.environ.get('REF_DIR', '../rp2350b-rm2-reference')
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else '.'
PROJECT = 'led-processor-board'
_seed = [0]


def uid():
    # deterministic UUIDs so re-running the generator gives the same files
    _seed[0] += 1
    return str(_uuid.uuid5(_uuid.NAMESPACE_URL, 'led-processor-board/%d' % _seed[0]))


def font(size=1.27):
    return S('font', S('size', size, size))


def effects(justify=None, hide=False, size=1.27):
    e = S('effects', font(size))
    if justify:
        e.append(S('justify', *[Sym(j) for j in justify.split()]))
    return e


def prop(name, value, x, y, ang=0.0, hide=False, justify=None, size=1.27):
    p = S('property', name, value, S('at', x, y, ang))
    if hide:
        p.append(S('hide', Sym('yes')))
    p += [S('show_name', Sym('no')), S('do_not_autoplace', Sym('no')), effects(justify, size=size)]
    return p


def r4(v):
    return round(v + 0.0, 4)


class Sheet:
    def __init__(self, path, page_uuid):
        self.path = path            # instance path, e.g. "/root" or "/root/sheet"
        self.items = []
        self.lib = {}               # full name -> lib symbol
        self.page_uuid = page_uuid

    def need(self, full, sym):
        if full not in self.lib:
            self.lib[full] = sym

    # ---- drawing primitives -------------------------------------------------
    def wire(self, *pts):
        for a, b in zip(pts, pts[1:]):
            self.items.append(S('wire', S('pts', S('xy', r4(a[0]), r4(a[1])), S('xy', r4(b[0]), r4(b[1]))),
                                S('stroke', S('width', 0.0), S('type', Sym('default'))), S('uuid', uid())))

    def junction(self, p):
        self.items.append(S('junction', S('at', r4(p[0]), r4(p[1])), S('diameter', 0.0),
                            S('color', 0.0, 0.0, 0.0, 0.0), S('uuid', uid())))

    def nc(self, p):
        self.items.append(S('no_connect', S('at', r4(p[0]), r4(p[1])), S('uuid', uid())))

    def label(self, name, p, ang=0.0):
        just = {0: 'left bottom', 90: 'left bottom', 180: 'right bottom', 270: 'right bottom'}[int(ang)]
        self.items.append(S('label', name, S('at', r4(p[0]), r4(p[1]), float(ang)),
                            effects(just), S('uuid', uid())))

    def glabel(self, name, p, ang=0.0, shape='bidirectional'):
        just = 'left' if int(ang) in (0, 90) else 'right'
        self.items.append(S('global_label', name, S('shape', Sym(shape)),
                            S('at', r4(p[0]), r4(p[1]), float(ang)), S('fields_autoplaced', Sym('yes')),
                            effects(just), S('uuid', uid()),
                            prop('Intersheetrefs', '${INTERSHEET_REFS}', r4(p[0]), r4(p[1]), 0.0, hide=True,
                                 justify=just)))

    def hlabel(self, name, p, ang=0.0, shape='bidirectional'):
        just = 'left' if int(ang) in (0, 90) else 'right'
        self.items.append(S('hierarchical_label', name, S('shape', Sym(shape)),
                            S('at', r4(p[0]), r4(p[1]), float(ang)), effects(just), S('uuid', uid())))

    def text(self, s, p, size=1.27):
        self.items.append(S('text', s, S('exclude_from_sim', Sym('no')), S('at', r4(p[0]), r4(p[1]), 0.0),
                            effects('left bottom', size=size), S('uuid', uid())))

    # ---- symbols --------------------------------------------------------------
    def symbol(self, full, libsym, ref, value, at, rot=0, mirror=None, fields=None, ref_at=None,
               val_at=None, power=False, in_bom=True, hide_value=False):
        self.need(full, libsym)
        pins = libs.pins(libsym)
        x, y = at
        e = S('symbol', S('lib_id', full), S('at', r4(x), r4(y), float(rot)))
        if mirror:
            e.append(S('mirror', Sym(mirror)))
        e += [S('unit', 1.0), S('body_style', 1.0), S('exclude_from_sim', Sym('no')),
              S('in_bom', Sym('yes' if in_bom else 'no')), S('on_board', Sym('yes')),
              S('in_pos_files', Sym('yes' if in_bom else 'no')), S('dnp', Sym('no')), S('uuid', uid())]
        rx, ry = ref_at if ref_at else (x + 2.54, y - 1.27)
        vx, vy = val_at if val_at else (x + 2.54, y + 1.27)
        fa = float(rot % 180)
        jf = None if (power or rot % 180) else 'left'
        e.append(prop('Reference', ref, r4(rx), r4(ry), fa, hide=power, justify=jf))
        e.append(prop('Value', value, r4(vx), r4(vy), fa, hide=hide_value, justify=jf))
        fields = dict(fields or {})
        for k in ('Footprint', 'Datasheet', 'Description'):
            if k not in fields:
                lp = sx.prop(libsym, k)
                fields[k] = lp[2] if lp else ''
        order = ['Footprint', 'Datasheet', 'Description'] + [k for k in fields if k not in
                                                              ('Footprint', 'Datasheet', 'Description')]
        for k in order:
            e.append(prop(k, fields[k], r4(x), r4(y), 0.0, hide=True))
        for n in sorted(pins, key=lambda s: (len(s), s)):
            e.append(S('pin', n, S('uuid', uid())))
        e.append(S('instances', S('project', PROJECT, S('path', self.path, S('reference', ref), S('unit', 1.0)))))
        self.items.append(e)
        return Placed(pins, x, y, rot, mirror)


class Placed:
    def __init__(self, pins, x, y, rot, mirror):
        self.pins, self.x, self.y, self.rot, self.mirror = pins, x, y, rot, mirror

    def __getitem__(self, n):
        px, py = self.pins[str(n)][:2]
        if self.mirror == 'y':
            px = -px
        elif self.mirror == 'x':
            py = -py
        a = math.radians(self.rot)
        vx = px * math.cos(a) - py * math.sin(a)
        vy = px * math.sin(a) + py * math.cos(a)
        return (r4(self.x + vx), r4(self.y - vy))


# --------------------------------------------------------------------------------------
ref = sx.parse(open(os.path.join(REF_DIR, 'RP2350B-RM2.kicad_sch')).read())
ROOT = sx.find(ref, 'uuid')[1]

SYM = {}
for full in ['Device:R', 'Device:C', 'Device:D_Schottky', 'Device:LED', 'RPI - Power:GND',
             'RPI - Power:PWR_NET', 'power:PWR_FLAG']:
    SYM[full] = libs.from_sch(ref, full)
for lib, name in [('Connector_Generic', 'Conn_02x08_Odd_Even'), ('Connector_Generic', 'Conn_01x10'),
                  ('Connector_Generic', 'Conn_01x03'), ('Connector', 'Screw_Terminal_01x02'),
                  ('Timer_RTC', 'DS3231M'), ('Device', 'Battery_Cell'),
                  ('Regulator_Switching', 'TPS54202DDC'), ('Device', 'L'),
                  ('Connector', 'USB_C_Receptacle_USB2.0_16P')]:
    SYM[lib + ':' + name] = libs.get_symbol(lib, name)

pwr_n = [100]
flg_n = [1]


def gnd(sh, p):
    pwr_n[0] += 1
    sh.symbol('RPI - Power:GND', SYM['RPI - Power:GND'], '#PWR%d' % pwr_n[0], 'GND', p, power=True,
              in_bom=False, val_at=(p[0], p[1] + 3.81))


def pwr(sh, name, p, rot=0):
    pwr_n[0] += 1
    sh.symbol('RPI - Power:PWR_NET', SYM['RPI - Power:PWR_NET'], '#PWR%d' % pwr_n[0], name, p, rot=rot,
              power=True, in_bom=False, val_at=(p[0], p[1] - 3.81))


def flag(sh, p):
    sh.symbol('power:PWR_FLAG', SYM['power:PWR_FLAG'], '#FLG%d' % flg_n[0], 'PWR_FLAG', p, power=True,
              in_bom=False, val_at=(p[0], p[1] - 3.81), hide_value=True)
    flg_n[0] += 1


def part(mpn, lcsc, mfr, desc, fp):
    f = {'Footprint': fp, 'Datasheet': '', 'Description': desc, 'manufacturer': mfr, 'manufacturer_pn': mpn}
    if lcsc:
        f['LCSC'] = lcsc
    return f


R0402 = 'Resistor_SMD:R_0402_1005Metric'
C0402 = 'Capacitor_SMD:C_0402_1005Metric'
C0603 = 'Capacitor_SMD:C_0603_1608Metric'
C0805 = 'Capacitor_SMD:C_0805_2012Metric'
P = {
    '33': part('0402WGF330JTCE', 'C25105', 'UNI-ROYAL', 'Resistor, 33, 5%, 0402', R0402),
    '10k': part('0402WGF1002TCE', 'C25744', 'UNI-ROYAL', 'Resistor, 10k, 1%, 0402', R0402),
    '1k': part('0402WGF1001TCE', 'C11702', 'UNI-ROYAL', 'Resistor, 1k, 1%, 0402', R0402),
    '100k': part('0402WGF1003TCE', 'C25741', 'UNI-ROYAL', 'Resistor, 100k, 1%, 0402', R0402),
    '13.3k': part('0402WGF1332TCE', 'C122547', 'UNI-ROYAL', 'Resistor, 13.3k, 1%, 0402', R0402),
    '5.1k': part('0402WGF5101TCE', 'C25905', 'UNI-ROYAL', 'Resistor, 5.1k, 1%, 0402 (USB-C CC pull-down)', R0402),
    '510k': part('0402WGF5103TCE', 'C11616', 'UNI-ROYAL', 'Resistor, 510k, 1%, 0402', R0402),
    '100n': part('CL05B104KO5NNNC', 'C1525', 'Samsung', 'Capacitor, 100n, 16V, X7R, 0402', C0402),
    '1u': part('CL05A105KA5NQNC', 'C52923', 'Samsung', 'Capacitor, 1u, 25V, X5R, 0402', C0402),
    '100n50': part('CC0603KRX7R9BB104', 'C14663', 'YAGEO', 'Capacitor, 100n, 50V, X7R, 0603', C0603),
    '75p': part('0402CG750J500NT', 'C37809', 'FH', 'Capacitor, 75p, 50V, C0G, 0402', C0402),
    '10u50': part('CL21A106KBYNNNE', 'C440198', 'Samsung', 'Capacitor, 10u, 50V, X5R, 0805', C0805),
    '22u25': part('CL21A226MAQNNNE', 'C45783', 'Samsung', 'Capacitor, 22u, 25V, X5R, 0805', C0805),
}


def R(sh, ref, val, at, rot=0, **kw):
    return sh.symbol('Device:R', SYM['Device:R'], ref, val, at, rot=rot, fields=P[val], **kw)


def C(sh, ref, val, at, rot=0, show=None, **kw):
    return sh.symbol('Device:C', SYM['Device:C'], ref, show or val, at, rot=rot, fields=P[val], **kw)


# ======================================================================================
# Root sheet: edit the reference
# ======================================================================================
root = Sheet('/' + ROOT, ROOT)

# 1. remove everything in the old J2/J3 header area
def positions(e):
    a = sx.find(e, 'at')
    if a:
        return [(a[1], a[2])]
    p = sx.find(e, 'pts')
    return [(x[1], x[2]) for x in p[1:]] if p else []


keep = []
removed = []
for e in ref[1:]:
    if isinstance(e, list) and e[0] in ('wire', 'label', 'symbol', 'junction', 'no_connect', 'text'):
        ps = positions(e)
        if ps and any(315 <= x <= 420 and 100 <= y <= 215 for x, y in ps):
            if e[0] == 'text' and e[1] == 'IOs':
                e[1] = 'RTC and headers'
                sx.find(e, 'at')[2] = 104.14
                keep.append(e)
                continue
            removed.append(e)
            continue
    keep.append(e)
assert sum(1 for e in removed if e[0] == 'symbol') == 13, len(removed)
ref[1:] = keep

# 1b. remove Pi's micro-B USB (J1) and its wiring; keep VBUS -> D2
def is_keep(e):
    if e[0] == 'symbol':
        return sx.prop(e, 'Reference')[2] == '#PWR2'          # the VBUS power symbol above D2
    if e[0] == 'wire':
        ps = positions(e)
        return sorted(ps) in (sorted([(39.37, 26.67), (39.37, 27.94)]), sorted([(39.37, 27.94), (41.91, 27.94)]))
    return False


keep = []
usb_removed = []
for e in ref[1:]:
    if isinstance(e, list) and e[0] in ('wire', 'label', 'symbol', 'junction', 'no_connect'):
        ps = positions(e)
        if ps and all(5 <= x <= 42 and 14 <= y <= 50 for x, y in ps) and not is_keep(e):
            usb_removed.append(e)
            continue
    keep.append(e)
assert [sx.prop(e, 'Reference')[2] for e in usb_removed if e[0] == 'symbol'] == ['J1', '#PWR1', '#PWR032'] or \
    sorted(sx.prop(e, 'Reference')[2] for e in usb_removed if e[0] == 'symbol') == sorted(['J1', '#PWR1', '#PWR032']), \
    [sx.prop(e, 'Reference')[2] for e in usb_removed if e[0] == 'symbol']
ref[1:] = keep

# 2. title block, project name in instances, VSYS note
tb = sx.find(ref, 'title_block')
tb[:] = S('title_block', S('title', 'LED processor board (RP2350B + RM2)'), S('date', '2026-10-19'),
          S('rev', 'A'),
          S('comment', 1.0, 'Based on RP2350B-RM2 / RPI-RP2350B-MINIMAL R4-S1'),
          S('comment', 2.0, 'Original design: Mike Stimson, Raspberry Pi (MIT)'))


def walk(e):
    if isinstance(e, list):
        if e and e[0] == 'project' and e[1] == 'RP2350B-RM2':
            e[1] = PROJECT
        for x in e:
            walk(x)


walk(ref)
for e in sx.findall(ref, 'text'):
    if e[1].startswith('VSYS is also J3 pin 2'):
        e[1] = ('VSYS comes from the 12 V input: buck to +5V, then\n'
                'D4 (Power sheet). D2 keeps that 5 V off the USB port;\n'
                'USB alone runs it through D2 (no +5V on ribbons).')

# 4. new root content: RTC, spare GPIO header, SWD header
sh = root
U6 = sh.symbol('Timer_RTC:DS3231M', SYM['Timer_RTC:DS3231M'], 'U6', 'DS3231SN', (370.84, 142.24),
               fields=part('DS3231SN#T&R', 'C9866', 'Analog Devices (Maxim)',
                           'RTC with TCXO and crystal, I2C, SOIC-16W (same as universal-strip-controller U9)',
                           'Package_SO:SOIC-16W_7.5x10.3mm_P1.27mm'),
               ref_at=(374.65, 128.27), val_at=(374.65, 130.81))
sh.wire(U6[16], (350.52, U6[16][1])); sh.label('GPIO33', (350.52, U6[16][1]), 180)
sh.wire(U6[15], (350.52, U6[15][1])); sh.label('GPIO32', (350.52, U6[15][1]), 180)
sh.nc(U6[4]); sh.nc(U6[1])
sh.wire(U6[3], (391.16, U6[3][1])); sh.label('GPIO34', (391.16, U6[3][1]), 0)
sh.wire(U6[2], (U6[2][0], 127.0)); pwr(sh, '+3V3', (U6[2][0], 127.0))
gnd(sh, U6[13])
# battery: VBAT up and over to BT1
BT1 = sh.symbol('Device:Battery_Cell', SYM['Device:Battery_Cell'], 'BT1', 'CR1220', (396.24, 134.62),
                fields=part('3001', '', 'Keystone', 'CR1220 coin cell holder (12 mm), through-hole; not at LCSC, hand-solder',
                            'Battery:BatteryHolder_Keystone_3001_1x12mm'),
                ref_at=(399.415, 130.81), val_at=(399.415, 133.35))
vb = U6[14]
sh.wire(vb, (vb[0], 124.46), (384.81, 124.46))
sh.wire((384.81, 124.46), (BT1[1][0], 124.46), BT1[1])
flag(sh, (384.81, 124.46)); sh.junction((384.81, 124.46))
gnd(sh, BT1[2])
# VCC decoupling
C26 = C(sh, 'C26', '1u', (345.44, 124.46), ref_at=(347.98, 123.19), val_at=(347.98, 125.73))
pwr(sh, '+3V3', C26[1]); gnd(sh, C26[2])
# pull-ups on SDA, SCL, INT
for k, (refd, net) in enumerate([('R17', 'GPIO32'), ('R18', 'GPIO33'), ('R19', 'GPIO34')]):
    x = 345.44 + 7.62 * k
    rr = R(sh, refd, '10k', (x, 163.83), ref_at=(x + 1.905, 162.56), val_at=(x + 1.905, 165.1))
    pwr(sh, '+3V3', rr[1])
    sh.wire(rr[2], (x, 172.72)); sh.label(net, (x, 172.72), 270)
sh.text('RTC: the DS3231 circuit from universal-strip-controller (U9), on I2C0:\n'
        'SDA = GPIO32, SCL = GPIO33, INT/SQW = GPIO34 (open drain, pulled up).\n'
        '32KHZ and RST unused. CR1220 in a Keystone 3001 holder.', (335.28, 114.3))
# spare GPIO header J2
J2 = sh.symbol('Connector_Generic:Conn_01x10', SYM['Connector_Generic:Conn_01x10'], 'J2', 'SPARE',
               (403.86, 182.88),
               fields=part('1x10 2.54 mm pin header', '', '', 'Spare GPIO35-42, 3V3, GND. Through-hole: hand-solder',
                           'Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical'),
               ref_at=(400.05, 166.37), val_at=(403.86, 166.37))
sh.wire(J2[1], (393.7, J2[1][1])); pwr(sh, '+3V3', (393.7, J2[1][1]))
for n, net in zip(range(2, 10), ['GPIO35', 'GPIO36', 'GPIO37', 'GPIO38', 'GPIO39', 'GPIO40_ADC0',
                                 'GPIO41_ADC1', 'GPIO42_ADC2']):
    sh.wire(J2[n], (388.62, J2[n][1])); sh.label(net, (388.62, J2[n][1]), 180)
sh.wire(J2[10], (393.7, J2[10][1])); gnd(sh, (393.7, J2[10][1]))
# SWD header J3 (Pico debug pin order)
J3 = sh.symbol('Connector_Generic:Conn_01x03', SYM['Connector_Generic:Conn_01x03'], 'J3', 'SWD',
               (403.86, 157.48),
               fields=part('1x3 2.54 mm pin header', '', '', 'SWD debug: SWCLK, GND, SWD (Pico order). Through-hole: hand-solder',
                           'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical'),
               ref_at=(400.05, 149.86), val_at=(403.86, 149.86))
sh.wire(J3[1], (388.62, J3[1][1])); sh.label('SWCLK', (388.62, J3[1][1]), 180)
sh.wire(J3[2], (393.7, J3[2][1]))
pwr_n[0] += 1
sh.symbol('RPI - Power:GND', SYM['RPI - Power:GND'], '#PWR%d' % pwr_n[0], 'GND', (393.7, J3[2][1]), rot=270,
          power=True, in_bom=False, val_at=(389.89, J3[2][1] - 1.27), hide_value=True)
sh.wire(J3[3], (388.62, J3[3][1])); sh.label('SWD', (388.62, J3[3][1]), 180)
sh.text('J2: spare GPIO35-42. UART1 TX/RX = GPIO36/37,\n'
        'GPIO40-42 = ADC0-2. J3: SWD in a Pico\'s debug pin order.\n'
        'GPIO0-31 go to the four output shrouds (Outputs sheet).\n'
        'GPIO43-47 are the RM2 and the PSRAM.', (335.28, 213.36))

# 4b. USB-C receptacle in place of Pi's micro-B
J1 = sh.symbol('Connector:USB_C_Receptacle_USB2.0_16P', SYM['Connector:USB_C_Receptacle_USB2.0_16P'], 'J1',
               'USB-C', (110.49, 38.1),
               fields=part('TYPE-C-31-M-12', 'C165948', 'Korean Hroparts Elec',
                           'USB-C receptacle, USB 2.0, 16 pin, SMD (replaces the micro-B)',
                           'Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12'),
               ref_at=(100.33, 17.78), val_at=(104.14, 17.78))
vb = J1['A4']
sh.wire(vb, (128.27, vb[1]), (130.81, vb[1])); pwr(sh, 'VBUS', (130.81, vb[1]))
flag(sh, (128.27, vb[1])); sh.junction((128.27, vb[1]))
for a, b, net in [('A7', 'B7', 'USB_D-'), ('A6', 'B6', 'USB_D+')]:
    pa, pb = J1[a], J1[b]
    top, bot = (pa, pb) if pa[1] < pb[1] else (pb, pa)
    sh.wire(top, (128.27, top[1]), (128.27, bot[1]), bot)
    sh.wire((128.27, top[1]), (130.81, top[1])); sh.junction((128.27, top[1]))
    sh.label(net, (130.81, top[1]), 0)
for pin, refd, x in [('A5', 'R56', 161.29), ('B5', 'R57', 153.67)]:
    p = J1[pin]
    sh.wire(p, (x, p[1]))
    rr = R(sh, refd, '5.1k', (x, p[1] + 3.81), ref_at=(x + 1.905, p[1] + 2.54), val_at=(x + 1.905, p[1] + 5.08))
    gnd(sh, rr[2])
sh.nc(J1['A8']); sh.nc(J1['B8'])
sh.wire(J1['SH'], (106.68, J1['A1'][1]), J1['A1']); gnd(sh, J1['A1'])
flag(sh, (106.68, J1['A1'][1])); sh.junction((106.68, J1['A1'][1]))
sh.text('CC1/CC2 5.1k pull-downs: a USB-C host sees a\nUSB 2.0 device and turns on 5 V.', (138.43, 50.8))

# 5. sheet symbols for Power and Outputs
SUB = {}
def sheet_symbol(name, fname, x, y, w, h, page, pins=()):
    su = uid()
    SUB[name] = su
    e = S('sheet', S('at', x, y), S('size', w, h), S('exclude_from_sim', Sym('no')),
          S('in_bom', Sym('yes')), S('on_board', Sym('yes')), S('dnp', Sym('no')),
          S('fields_autoplaced', Sym('yes')),
          S('stroke', S('width', 0.1524), S('type', Sym('solid'))), S('fill', S('color', 0.0, 0.0, 0.0, 0.0)),
          S('uuid', su),
          prop('Sheetname', name, x, r4(y - 0.7116), 0.0, justify='left bottom'),
          prop('Sheetfile', fname, x, r4(y + h + 0.5846), 0.0, justify='left top'))
    for pname, px, py, ang in pins:
        e.append(S('pin', pname, Sym('bidirectional'), S('at', r4(px), r4(py), float(ang)), S('uuid', uid()),
                   effects('left' if ang == 180 else 'right')))
    e.append(S('instances', S('project', PROJECT, S('path', '/' + ROOT, S('page', str(page))))))
    sh.items.append(e)

sheet_symbol('Power', 'Power.kicad_sch', 355.6, 71.12, 22.86, 12.7, 2)
ox, oy, ow, oh = 285.75, 180.34, 30.48, 43.18
opins = []
for i in range(16):
    y = oy + 2.54 + 2.54 * i
    opins.append(('GPIO%d' % i, ox, y, 180))
    opins.append(('GPIO%d' % (16 + i), ox + ow, y, 0))
sheet_symbol('Outputs', 'Outputs.kicad_sch', ox, oy, ow, oh, 3, opins)
for i in range(16):
    y = oy + 2.54 + 2.54 * i
    sh.wire((ox, y), (ox - 5.08, y)); sh.label('GPIO%d' % i, (ox - 5.08, y), 180)
    sh.wire((ox + ow, y), (ox + ow + 5.08, y)); sh.label('GPIO%d' % (16 + i), (ox + ow + 5.08, y), 0)
sh.text('GPIO0-31 to the four output shrouds', (ox - 5.08, oy - 3.81))
sh.text('12 V in, 5 V buck', (355.6, 63.5), size=2.54)

# ======================================================================================
# Power sheet
# ======================================================================================
pw = Sheet('/' + ROOT + '/' + SUB['Power'], None)
sh = pw
sh.text('12 V in, 5 V buck', (25.4, 50.8), size=2.54)
J4 = sh.symbol('Connector:Screw_Terminal_01x02', SYM['Connector:Screw_Terminal_01x02'], 'J4', '12V IN',
               (25.4, 76.2), mirror='y',
               fields=part('DB301V-3.5-2P-GN-S', 'C695629', 'DORABO',
                           '12 V input from the LED supply (via the output board PTC), 3.5 mm screw terminal',
                           'TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-2-3.5-H_1x02_P3.50mm_Horizontal'),
               ref_at=(17.78, 72.39), val_at=(17.78, 74.93))
sh.wire(J4[2], (33.02, J4[2][1]), (33.02, 83.82)); gnd(sh, (33.02, 83.82))
D3 = sh.symbol('Device:D_Schottky', SYM['Device:D_Schottky'], 'D3', 'SS34', (44.45, 76.2), rot=180,
               fields=part('SS34', 'C8678', 'MDD', 'Schottky 40 V 3 A, reverse-polarity protection', 'Diode_SMD:D_SMA'),
               ref_at=(41.91, 71.12), val_at=(41.91, 73.66))
sh.wire(J4[1], D3[2]); sh.label('VIN_RAW', (33.02, 76.2), 0)
# +12V bus with taps
taps12 = [D3[1][0], 50.8, 53.34, 58.42, 66.04, 73.66, 81.28, 91.44]
for a, b in zip(taps12, taps12[1:]):
    sh.wire((a, 76.2), (b, 76.2))
for x in taps12[1:-1]:
    sh.junction((x, 76.2))
flag(sh, (50.8, 76.2)); pwr(sh, '+12V', (53.34, 76.2))
for refd, val, x in [('C27', '10u50', 58.42), ('C28', '10u50', 66.04), ('C29', '100n50', 73.66)]:
    cc = C(sh, refd, val, (x, 80.01), show='10u' if val == '10u50' else '100n',
           ref_at=(x + 1.27, 78.74), val_at=(x + 1.27, 81.28))
    gnd(sh, cc[2])
U7 = sh.symbol('Regulator_Switching:TPS54202DDC', SYM['Regulator_Switching:TPS54202DDC'], 'U7', 'TPS54202H',
               (106.68, 86.36),
               fields=part('TPS54202HDDCR', 'C527684', 'Texas Instruments',
                           '4.5-28 V in, 2 A sync buck, 500 kHz, SOT-23-6 (EN has an internal pull-down: H version)',
                           'Package_TO_SOT_SMD:SOT-23-6'),
               ref_at=(101.6, 77.47), val_at=(101.6, 80.01))
sh.wire((91.44, 76.2), (91.44, U7[3][1]), U7[3])
R20 = R(sh, 'R20', '510k', (81.28, 80.01), ref_at=(82.55, 78.74), val_at=(82.55, 81.28))
sh.wire(R20[2], (81.28, U7[5][1]), U7[5])
gnd(sh, U7[1])
# BOOT cap from BOOT to SW
C30 = C(sh, 'C30', '100n50', (127.0, 82.55), show='100n', ref_at=(128.27, 81.28), val_at=(128.27, 83.82))
sh.wire(U7[6], (119.38, U7[6][1]), (119.38, C30[1][1]), C30[1])
L2 = sh.symbol('Device:L', SYM['Device:L'], 'L2', '15u', (135.89, 86.36), rot=90,
               fields=part('SWPA8040S150MT', 'C16853', 'Sunlord', 'Inductor, 15 uH, 2.6 A, shielded, 8 x 8 mm',
                           'Inductor_SMD:L_Sunlord_SWPA8040S'),
               ref_at=(134.62, 82.55), val_at=(138.43, 82.55))
sh.wire(U7[2], C30[2]); sh.wire(C30[2], L2[1]); sh.junction(C30[2])
sh.label('SW', (119.38, 86.36), 0)
# +5V bus
taps5 = [L2[2][0], 144.78, 154.94, 162.56, 170.18, 177.8, 182.88, 185.42, 193.04, 200.66]
for a, b in zip(taps5, taps5[1:]):
    sh.wire((a, 86.36), (b, 86.36))
for x in taps5[1:-1]:
    sh.junction((x, 86.36))
R21 = R(sh, 'R21', '100k', (144.78, 90.17), ref_at=(146.05, 88.9), val_at=(146.05, 91.44))
R22 = R(sh, 'R22', '13.3k', (144.78, 97.79), ref_at=(146.05, 96.52), val_at=(146.05, 99.06))
C31 = C(sh, 'C31', '75p', (154.94, 90.17), ref_at=(156.21, 88.9), val_at=(156.21, 91.44))
sh.wire(U7[4], (119.38, U7[4][1]), (119.38, 93.98), R21[2])
sh.wire(R21[2], C31[2]); sh.junction(R21[2]); sh.label('FB', (121.92, 93.98), 0)
gnd(sh, R22[2])
for refd, x in [('C32', 162.56), ('C33', 170.18), ('C34', 177.8)]:
    cc = C(sh, refd, '22u25', (x, 90.17), show='22u', ref_at=(x + 1.27, 88.9), val_at=(x + 1.27, 91.44))
    gnd(sh, cc[2])
pwr(sh, '+5V', (182.88, 86.36)); flag(sh, (185.42, 86.36))
R23 = R(sh, 'R23', '1k', (193.04, 90.17), ref_at=(194.31, 88.9), val_at=(194.31, 91.44))
D5 = sh.symbol('Device:LED', SYM['Device:LED'], 'D5', 'Green', (193.04, 97.79), rot=90,
               fields=part('19-217/GHC-YR1S2/3T', 'C72043', 'Everlight', '+5V present (green LED, 0603)',
                           'LED_SMD:LED_0603_1608Metric'),
               ref_at=(197.485, 96.52), val_at=(199.39, 99.06))
assert R23[2] == D5[2], (R23[2], D5[2])
gnd(sh, D5[1])
D4 = sh.symbol('Device:D_Schottky', SYM['Device:D_Schottky'], 'D4', 'B5819W', (204.47, 86.36), rot=180,
               fields=part('B5819W SL', 'C8598', 'Changjiang', 'Schottky 40 V 1 A: +5V to VSYS (like D2 from USB)',
                           'Diode_SMD:D_SOD-123'),
               ref_at=(201.93, 81.28), val_at=(201.93, 83.82))
sh.wire(D4[1], (213.36, 86.36)); pwr(sh, 'VSYS', (213.36, 86.36))
sh.text('U7 values follow TI\'s TPS54202HEVM-716 5 V design (100k / 13.3k with 75 pF across the top\n'
        'resistor: 0.596 V x (1 + 100/13.3) = 5.08 V; 15 uH; 500 kHz). R20 enables U7 from the 12 V side.\n'
        'D3 blocks a reversed 12 V lead. The load is small: the 3.3 V regulator on VSYS (RP2350, RM2, flash,\n'
        'PSRAM) and the four output boards\' shifters (VCCB on +5V, VCCA on +3V3), well under 0.5 A.\n'
        '+5V feeds the ribbons directly; VSYS gets it through D4 so USB can\'t back-feed the buck.', (25.4, 119.38))

# ======================================================================================
# Outputs sheet
# ======================================================================================
op = Sheet('/' + ROOT + '/' + SUB['Outputs'], None)
sh = op
sh.text('Output shrouds: 16-pin IDC to the output boards. Pin 1 is the triangle on the shroud.\n'
        'Odd pins 1-11 GND, 13 = +3V3 (shifter VCCA), 15 = +5V (shifter VCCB); data 1-8 on even pins 2-16\n'
        '(same as led-output-board J2 and MegaTree docs/power.md). 33R series resistors at the RP2350 end.',
        (20.32, 25.4))
rn = 24
cn = 36
BOX = part('Box header 2x08 2.54 mm', '', '', 'Shrouded 16-pin IDC box header, same as the output boards. Through-hole: hand-solder',
           'Connector_IDC:IDC-Header_2x08_P2.54mm_Vertical')
for k in range(4):
    bx = [20.32, 210.82][k % 2]
    by = [43.18, 156.21][k // 2]
    g0 = 8 * k
    sh.text('OUT%d: GPIO%d-%d' % (k + 1, g0, g0 + 7), (bx, by - 2.54), size=2.032)
    J = sh.symbol('Connector_Generic:Conn_02x08_Odd_Even', SYM['Connector_Generic:Conn_02x08_Odd_Even'],
                  'J%d' % (5 + k), 'OUT%d' % (k + 1), (bx + 30.48, by + 20.32), fields=BOX,
                  ref_at=(bx + 29.21, by + 8.89), val_at=(bx + 29.21, by + 34.29))
    xb = bx + 22.86
    for n in (1, 3, 5, 7, 9, 11):
        sh.wire(J[n], (xb, J[n][1]))
    sh.wire((xb, J[1][1]), (xb, J[11][1]))
    for n in (3, 5, 7, 9):
        sh.junction((xb, J[n][1]))
    sh.wire((xb, J[1][1]), (bx + 15.24, J[1][1])); sh.junction((xb, J[1][1]))
    gnd(sh, (bx + 15.24, J[1][1]))
    sh.wire(J[13], (bx + 17.78, J[13][1])); pwr(sh, '+3V3', (bx + 17.78, J[13][1]))
    sh.wire(J[15], (bx + 12.7, J[15][1])); pwr(sh, '+5V', (bx + 12.7, J[15][1]))
    for i, (net, val) in enumerate([('+3V3', '100n'), ('+5V', '100n')]):
        cx = bx + 10.16 + 10.16 * i
        cc = C(sh, 'C%d' % cn, val, (cx, by + 45.72), ref_at=(cx + 1.27, by + 44.45), val_at=(cx + 1.27, by + 46.99))
        cn += 1
        pwr(sh, net, cc[1]); gnd(sh, cc[2])
    # data: each even pin straight through its 33R to the hierarchical label. The resistors
    # alternate between two columns so their text has room at the 2.54 mm pin pitch.
    for n in range(1, 9):
        p = J[2 * n]
        px, py = p
        if n % 2:
            cx, rx, vx = px + 12.7, px + 2.794, px + 6.35
        else:
            cx, rx, vx = px + 22.86, px + 29.21, px + 32.766
        rr = R(sh, 'R%d' % rn, '33', (cx, py), rot=90, ref_at=(rx, py - 1.27), val_at=(vx, py - 1.27))
        rn += 1
        sh.wire(p, rr[1])
        sh.wire(rr[2], (px + 38.1, py)); sh.hlabel('GPIO%d' % (g0 + n - 1), (px + 38.1, py), 0)

# ======================================================================================
# write files
# ======================================================================================
def lib_symbols_of(sheet, base=None):
    ls = S('lib_symbols')
    names = {}
    if base is not None:
        for e in sx.findall(base, 'symbol'):
            names[e[1]] = e
    for full, s in sheet.lib.items():
        names.setdefault(full, s)
    for full in sorted(names):
        ls.append(names[full])
    return ls


def subsheet_file(sheet, title, page):
    tree = S('kicad_sch', S('version', 20260306.0), S('generator', 'eeschema'), S('generator_version', '10.0'),
             S('uuid', uid()), S('paper', 'A3' if title == 'Outputs' else 'A4'),
             S('title_block', S('title', 'LED processor board: ' + title), S('date', '2026-10-19'), S('rev', 'A')),
             lib_symbols_of(sheet))
    tree += sheet.items
    tree.append(S('embedded_fonts', Sym('no')))
    return tree


# root: merge new lib symbols and items
ls = sx.find(ref, 'lib_symbols')
idx = ref.index(ls)
ref[idx] = lib_symbols_of(root, ls)
si = next(i for i, e in enumerate(ref) if isinstance(e, list) and e[0] == 'sheet_instances')
ref[si:si] = root.items

os.makedirs(OUT_DIR, exist_ok=True)
def write(name, tree):
    with open(os.path.join(OUT_DIR, name), 'w') as f:
        f.write(sx.dump(tree) + '\n')

write(PROJECT + '.kicad_sch', ref)
write('Power.kicad_sch', subsheet_file(pw, 'Power', 2))
write('Outputs.kicad_sch', subsheet_file(op, 'Outputs', 3))

# project file
pro = json.load(open(os.path.join(REF_DIR, 'RP2350B-RM2.kicad_pro')))
pro['meta']['filename'] = PROJECT + '.kicad_pro'
pro['sheets'] = [[ROOT, PROJECT], [SUB['Power'], 'Power'], [SUB['Outputs'], 'Outputs']]
if 'top_level_sheets' in pro.get('schematic', {}):
    for t in pro['schematic']['top_level_sheets']:
        if isinstance(t, dict):
            for k in ('filename', 'name'):
                if k in t and 'RP2350B-RM2' in str(t[k]):
                    t[k] = str(t[k]).replace('RP2350B-RM2', PROJECT)
json.dump(pro, open(os.path.join(OUT_DIR, PROJECT + '.kicad_pro'), 'w'), indent=2)
for f in ['rp2350b_rm2.kicad_sym', 'sym-lib-table', 'fp-lib-table', 'LICENSE-RaspberryPi.txt']:
    shutil.copy(os.path.join(REF_DIR, f), OUT_DIR)
shutil.copytree(os.path.join(REF_DIR, 'rp2350b_rm2.pretty'), os.path.join(OUT_DIR, 'rp2350b_rm2.pretty'),
                dirs_exist_ok=True)
print('ok', rn - 1, cn - 1, pwr_n[0], flg_n[0])
