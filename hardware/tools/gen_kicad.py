#!/usr/bin/env python3
"""Generate the KiCad project and BOM from one netlist description.

The schematic, the symbol library and bom.csv all come from PARTS below, so
the BOM cannot drift from the schematic. Connections are made with net labels
placed on every pin end (netlist-style schematic). Run:

    python3 hardware/tools/gen_kicad.py

Outputs (overwritten): hardware/kicad/fan_controller.{kicad_pro,kicad_sch,kicad_pcb},
hardware/kicad/fanctl.kicad_sym, hardware/kicad/sym-lib-table, hardware/bom.csv.
After opening in KiCad 8+, use "Update PCB from Schematic" (F8) to pull in footprints.
"""
import collections
import csv
import json
import pathlib
import re
import uuid

HW = pathlib.Path(__file__).resolve().parent.parent
OUT = HW / "kicad"
PROJECT = "fan_controller"
LIB = "fanctl"
NS = uuid.UUID("6f1c0d2e-3a51-4c1b-9d0e-2b7f5a3c8e41")  # stable UUIDs across runs
ROOT_UUID = str(uuid.uuid5(NS, "root"))
G = 2.54

# --------------------------------------------------------------------------
# Symbols: (left pins, right pins, body width, show pin names)
# Pins are (number, name). Numbers match the footprint pad numbers.
# --------------------------------------------------------------------------

def two_pin(a="~", b="~"):
    return dict(left=[("1", a)], right=[("2", b)], w=2 * G, names=a != "~")

SYMBOLS = {
    "R": two_pin(),
    "C": two_pin(),
    "C_Pol": two_pin("+", "-"),
    "FerriteBead": two_pin(),
    "Polyfuse": two_pin(),
    # KiCad diode/LED footprints: pad 1 = cathode.
    "D": dict(left=[("2", "A")], right=[("1", "K")], w=2 * G, names=True),
    "D_Zener": dict(left=[("2", "A")], right=[("1", "K")], w=2 * G, names=True),
    "D_Schottky": dict(left=[("2", "A")], right=[("1", "K")], w=2 * G, names=True),
    "D_TVS": dict(left=[("2", "A")], right=[("1", "K")], w=2 * G, names=True),
    "LED": dict(left=[("2", "A")], right=[("1", "K")], w=2 * G, names=True),
    "BAT54C": dict(left=[("1", "A1"), ("2", "A2")], right=[("3", "K")], w=4 * G, names=True),
    "BAT54S": dict(left=[("1", "A1"), ("2", "K2")], right=[("3", "K1A2")], w=4 * G, names=True),
    "Q_PMOS_GSD": dict(left=[("1", "G")], right=[("2", "S"), ("3", "D")], w=4 * G, names=True),
    "Q_NMOS_GSD": dict(left=[("1", "G")], right=[("3", "D"), ("2", "S")], w=4 * G, names=True),
    "Crystal_GND24": dict(left=[("1", "1"), ("2", "GND")], right=[("3", "2"), ("4", "GND")], w=4 * G, names=True),
    "AMS1117-3.3": dict(left=[("3", "VI"), ("1", "GND")], right=[("2", "VO")], w=6 * G, names=True),
    "USBLC6-2SC6": dict(left=[("1", "IO1"), ("3", "IO2"), ("5", "VBUS")],
                        right=[("6", "IO1"), ("4", "IO2"), ("2", "GND")], w=6 * G, names=True),
    "USB_B_Micro": dict(left=[("1", "VBUS"), ("2", "D-"), ("3", "D+"), ("4", "ID"), ("5", "GND"), ("6", "Shield")],
                        right=[], w=6 * G, names=True),
    "Fan_4pin": dict(left=[("1", "GND"), ("2", "+12V"), ("3", "Tach"), ("4", "PWM")], right=[], w=6 * G, names=True),
    "Conn_01x02": dict(left=[("1", "1"), ("2", "2")], right=[], w=4 * G, names=True),
    "Conn_01x03": dict(left=[(str(i), str(i)) for i in range(1, 4)], right=[], w=4 * G, names=True),
    "Conn_01x04": dict(left=[(str(i), str(i)) for i in range(1, 5)], right=[], w=4 * G, names=True),
    "Conn_01x05": dict(left=[(str(i), str(i)) for i in range(1, 6)], right=[], w=4 * G, names=True),
    "TestPoint": dict(left=[("1", "1")], right=[], w=2 * G, names=False),
    "STM32F103C8Tx": dict(
        left=[("1", "VBAT"), ("24", "VDD"), ("36", "VDD"), ("48", "VDD"), ("9", "VDDA"),
              ("8", "VSSA"), ("23", "VSS"), ("35", "VSS"), ("47", "VSS"),
              ("7", "NRST"), ("44", "BOOT0"), ("5", "PD0-OSC_IN"), ("6", "PD1-OSC_OUT"),
              ("2", "PC13"), ("3", "PC14"), ("4", "PC15")],
        right=[("10", "PA0"), ("11", "PA1"), ("12", "PA2"), ("13", "PA3"), ("14", "PA4"), ("15", "PA5"),
               ("16", "PA6"), ("17", "PA7"), ("29", "PA8"), ("30", "PA9"), ("31", "PA10"), ("32", "PA11"),
               ("33", "PA12"), ("34", "PA13"), ("37", "PA14"), ("38", "PA15"),
               ("18", "PB0"), ("19", "PB1"), ("20", "PB2"), ("39", "PB3"), ("40", "PB4"), ("41", "PB5"),
               ("42", "PB6"), ("43", "PB7"), ("45", "PB8"), ("46", "PB9"), ("21", "PB10"), ("22", "PB11"),
               ("25", "PB12"), ("26", "PB13"), ("27", "PB14"), ("28", "PB15")],
        w=12 * G, names=True),
}

PREFIX = {"R": "R", "C": "C", "C_Pol": "C", "FerriteBead": "FB", "Polyfuse": "F", "D": "D", "D_Zener": "D",
          "D_Schottky": "D", "D_TVS": "D", "LED": "D", "BAT54C": "D", "BAT54S": "D", "Q_PMOS_GSD": "Q",
          "Q_NMOS_GSD": "Q", "Crystal_GND24": "Y", "AMS1117-3.3": "U", "USBLC6-2SC6": "U", "USB_B_Micro": "J",
          "Fan_4pin": "J", "Conn_01x02": "J", "Conn_01x03": "J", "Conn_01x04": "J", "Conn_01x05": "J",
          "TestPoint": "TP", "STM32F103C8Tx": "U"}

# --------------------------------------------------------------------------
# Footprints (KiCad 8 standard libraries)
# --------------------------------------------------------------------------
FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_R1206 = "Resistor_SMD:R_1206_3216Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C1206 = "Capacitor_SMD:C_1206_3216Metric"
FP_CP = "Capacitor_SMD:CP_Elec_6.3x7.7"
FP_TANT = "Capacitor_Tantalum_SMD:CP_EIA-3528-21_Kemet-B"
FP_FB = "Inductor_SMD:L_0603_1608Metric"
FP_SOT23 = "Package_TO_SOT_SMD:SOT-23"
FP_SOD123 = "Diode_SMD:D_SOD-123"
FP_SMA = "Diode_SMD:D_SMA"
FP_SMB = "Diode_SMD:D_SMB"
FP_LED = "LED_SMD:LED_0603_1608Metric"
FP_PTC = "Fuse:Fuse_1812_4532Metric"
FP_SOT223 = "Package_TO_SOT_SMD:SOT-223-3_TabPin2"
FP_SOT236 = "Package_TO_SOT_SMD:SOT-23-6"
FP_LQFP48 = "Package_QFP:LQFP-48_7x7mm_P0.5mm"
FP_XTAL = "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm"
FP_FAN = "Connector:FanPinHeader_1x04_P2.54mm_Vertical"
FP_TERM = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal"
FP_USB = "Connector_USB:USB_Micro-B_Molex-105017-0001"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"


def fp_header(n):
    return f"Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical"

# --------------------------------------------------------------------------
# Netlist
# --------------------------------------------------------------------------
Part = collections.namedtuple("Part", "block sym value fp pins mpn desc dnp")
PARTS = []


def part(block, sym, value, fp, pins, mpn="", desc="", dnp=False):
    PARTS.append(Part(block, sym, value, fp, pins, mpn, desc, dnp))


def res(block, value, a, b, desc="", fp=FP_R):
    part(block, "R", value, fp, {"1": a, "2": b}, desc=desc)


def cap(block, value, a, b="GND", fp=FP_C, desc=""):
    part(block, "C", value, fp, {"1": a, "2": b}, desc=desc)


# Power input, protection, 3.3 V regulator, 12 V sense
B = "Power"
part(B, "Conn_01x02", "12V IN", FP_TERM, {"1": "VIN_12V", "2": "GND"}, "Phoenix 1715721", "12 V input from PSU")
part(B, "Polyfuse", "2.5A", FP_PTC, {"1": "VIN_12V", "2": "+12V"}, "Bourns MF-MSMF250/16X-2", "PTC 2.5 A hold, 16 V")
part(B, "D_TVS", "SMBJ13A", FP_SMB, {"1": "+12V", "2": "GND"}, "Littelfuse SMBJ13A",
     "TVS 13 V standoff; also crowbars reverse polarity into F1")
part(B, "C_Pol", "100u 25V", FP_CP, {"1": "+12V", "2": "GND"}, desc="Bulk, 12 V rail")
cap(B, "10u 25V", "+12V", fp=FP_C1206)
# The TVS clamps up to 21.5 V, above the LDO's 15 V limit. The series resistor and
# 13 V zener hold VREG_IN below 15 V during surges and take ~1.9 V off the LDO.
res(B, "47", "+12V", "V12_LDO", "LDO feed; limits zener current, 0.08 W at 40 mA", fp=FP_R1206)
part(B, "BAT54C", "BAT54C", FP_SOT23, {"1": "VBUS", "2": "V12_LDO", "3": "VREG_IN"}, "Nexperia BAT54C,215",
     "Diode-OR: MCU runs from USB or 12 V")
part(B, "D_Zener", "BZT52C13", FP_SOD123, {"1": "VREG_IN", "2": "GND"}, "Diodes Inc BZT52C13-7-F",
     "LDO input clamp, 13 V")
cap(B, "10u 25V", "VREG_IN", fp=FP_C1206)
part(B, "AMS1117-3.3", "AMS1117-3.3", FP_SOT223, {"1": "GND", "2": "+3V3", "3": "VREG_IN"}, "AMS1117-3.3",
     "LDO 3.3 V; 15 V max in, ~0.3 W at 12 V in")
part(B, "C_Pol", "22u 10V", FP_TANT, {"1": "+3V3", "2": "GND"}, "KEMET T491B226K010AT",
     "Tantalum: AMS1117 needs output ESR for stability")
part(B, "FerriteBead", "600R@100MHz", FP_FB, {"1": "+3V3", "2": "+3V3A"}, "Murata BLM18AG601SN1D", "VDDA filter")
cap(B, "1u", "+3V3A")
cap(B, "10n", "+3V3A")
res(B, "47k", "+12V", "V12_SENSE", "12 V sense divider (top)")
res(B, "10k", "V12_SENSE", "GND", "12 V sense divider (bottom)")
cap(B, "100n", "V12_SENSE")

# MCU
B = "MCU"
mcu = {
    "1": "+3V3", "24": "+3V3", "36": "+3V3", "48": "+3V3", "9": "+3V3A",
    "8": "GND", "23": "GND", "35": "GND", "47": "GND",
    "7": "NRST", "44": "BOOT0", "5": "OSC_IN", "6": "OSC_OUT",
    "2": "LED_K", "3": None, "4": None,
    "10": "AIN1", "11": "AIN2", "12": "AIN3", "13": "AIN4", "14": "AIN5", "15": "AIN6",
    "16": None, "17": None, "29": None, "30": None, "31": None,
    "32": "USB_DM", "33": "USB_DP", "34": "SWDIO", "37": "SWCLK", "38": "FAN1_PWR",
    "18": "V12_SENSE", "19": None, "20": "BOOT1", "39": "FAN2_PWR", "40": None, "41": None,
    "42": "FAN1_PWM", "43": "FAN2_PWM", "45": "FAN3_PWM", "46": "FAN4_PWM",
    "21": "FAN3_PWR", "22": "FAN4_PWR",
    "25": "FAN1_TACH", "26": "FAN2_TACH", "27": "FAN3_TACH", "28": "FAN4_TACH",
}
part(B, "STM32F103C8Tx", "STM32F103C8T6", FP_LQFP48, mcu, "STMicroelectronics STM32F103C8T6")
for _ in range(4):
    cap(B, "100n", "+3V3", desc="VDD/VBAT decoupling, one per pin")
cap(B, "4u7", "+3V3", fp=FP_C1206)
part(B, "Crystal_GND24", "8MHz", FP_XTAL, {"1": "OSC_IN", "2": "GND", "3": "OSC_OUT", "4": "GND"},
     "Abracon ABM8-8.000MHZ-B2-T", "8 MHz, CL 18 pF")
cap(B, "27p", "OSC_IN", desc="C0G; 2 x (CL - ~4 pF stray)")
cap(B, "27p", "OSC_OUT", desc="C0G; 2 x (CL - ~4 pF stray)")
cap(B, "100n", "NRST")
res(B, "10k", "BOOT0", "GND")
res(B, "10k", "BOOT1", "GND")
res(B, "1k", "+3V3", "LED_A")
part(B, "LED", "green", FP_LED, {"2": "LED_A", "1": "LED_K"}, desc="Status, PC13 sinks")
part(B, "Conn_01x05", "SWD", fp_header(5), {"1": "+3V3", "2": "SWCLK", "3": "GND", "4": "SWDIO", "5": "NRST"},
     desc="SWD: 3V3 SWCLK GND SWDIO NRST")

# USB
B = "USB"
part(B, "USB_B_Micro", "USB micro-B", FP_USB,
     {"1": "VBUS", "2": "USB_DM", "3": "USB_DP", "4": None, "5": "GND", "6": "GND"}, "Molex 105017-0001")
part(B, "Conn_01x04", "USB int", fp_header(4), {"1": "VBUS", "2": "USB_DM", "3": "USB_DP", "4": "GND"},
     desc="Alt. to J: cable to motherboard USB 2.0 header (VBUS D- D+ GND)")
part(B, "USBLC6-2SC6", "USBLC6-2SC6", FP_SOT236,
     {"1": "USB_DP", "6": "USB_DP", "3": "USB_DM", "4": "USB_DM", "5": "VBUS", "2": "GND"},
     "STMicroelectronics USBLC6-2SC6", "USB ESD protection")
res(B, "1k5", "USB_DP", "+3V3", "USB FS D+ pull-up")

# Fan channels
for n in range(1, 5):
    B = f"Fan {n}"
    f = f"FAN{n}"
    # High-side switch with gate protection:
    #   R_GS/R_G divider limits Vgs to -Vin/2 (-6 V at 12 V), inside AO3401A's +-12 V rating;
    #   the 10 V zener clamps transients on the 12 V rail; R_G limits zener current.
    part(B, "Q_PMOS_GSD", "AO3401A", FP_SOT23, {"1": f"{f}_GATE", "2": "+12V", "3": f"{f}_SW_12V"},
         "Alpha & Omega AO3401A", "High-side P-MOSFET, -30 V, -4 A")
    part(B, "Polyfuse", "1.1A", FP_PTC, {"1": f"{f}_SW_12V", "2": f"{f}_12V"}, "Bourns MF-MSMF110/16-2",
         "PTC 1.1 A hold, 16 V; a shorted fan does not trip F1")
    res(B, "4k7", "+12V", f"{f}_GATE", "Gate-source pull-up (P-FET off by default)")
    res(B, "4k7", f"{f}_GATE", f"{f}_DRV", "Gate divider / zener current limit")
    part(B, "D_Zener", "BZT52C10", FP_SOD123, {"1": "+12V", "2": f"{f}_GATE"}, "Diodes Inc BZT52C10-7-F",
         "Gate-source clamp, 10 V")
    part(B, "Q_NMOS_GSD", "BSS138", FP_SOT23, {"1": f"{f}_PWR_G", "2": "GND", "3": f"{f}_DRV"},
         "onsemi BSS138", "Level shifter for P-FET gate")
    res(B, "100", f"{f}_PWR", f"{f}_PWR_G")
    res(B, "10k", f"{f}_PWR_G", "+3V3", "Fan powered while MCU is in reset")
    part(B, "D_Schottky", "SS14", FP_SMA, {"1": f"{f}_12V", "2": "GND"}, "SS14", "Freewheel diode, fan inductance")
    part(B, "Q_NMOS_GSD", "BSS138", FP_SOT23, {"1": f"{f}_PWM_G", "2": "GND", "3": f"{f}_PWM_OUT"},
         "onsemi BSS138", "Open-drain PWM buffer (fan pulls up to 5 V)")
    res(B, "100", f"{f}_PWM", f"{f}_PWM_G")
    res(B, "100k", f"{f}_PWM_G", "GND", "PWM line released = full speed in reset")
    res(B, "10k", f"{f}_TACH_IN", "+3V3", "Tach pull-up (open collector)")
    res(B, "1k", f"{f}_TACH_IN", f"{f}_TACH")
    cap(B, "1n", f"{f}_TACH", desc="Tach RC filter")
    part(B, "Fan_4pin", f"FAN{n}", FP_FAN,
         {"1": "GND", "2": f"{f}_12V", "3": f"{f}_TACH_IN", "4": f"{f}_PWM_OUT"}, "Molex 47053-1000",
         "4-pin PC fan header")

# Analog sensor inputs
for k in range(1, 7):
    B = "Analog inputs"
    a = f"AIN{k}"
    part(B, "Conn_01x03", f"AIN{k}", fp_header(3), {"1": "+3V3A", "2": f"{a}_IN", "3": "GND"},
         desc="Sensor: 3V3A SIG GND")
    res(B, "10k", f"{a}_IN", "+3V3A", "NTC 10k bias; DNP for voltage-output sensors")
    res(B, "1k", f"{a}_IN", a, "Input series resistor")
    cap(B, "100n", a, desc="ADC anti-alias / sample charge")
    part(B, "BAT54S", "BAT54S", FP_SOT23, {"1": "GND", "2": "+3V3A", "3": a}, "Nexperia BAT54S,215",
         "Clamp to 0..3.3 V (analog pins are not 5 V tolerant)")

# Test points (last, so they do not shift other references)
B = "Test points"
for net in ("+12V", "+3V3", "GND", "NRST", "FAN1_PWM", "FAN1_TACH"):
    part(B, "TestPoint", net, FP_TP, {"1": net}, desc="Probe pad")

# --------------------------------------------------------------------------
# Reference assignment and checks
# --------------------------------------------------------------------------

def assign_refs():
    counters = collections.Counter()
    refs = []
    for p in PARTS:
        prefix = PREFIX[p.sym]
        counters[prefix] += 1
        refs.append(f"{prefix}{counters[prefix]}")
    return refs


def check(refs):
    errors = []
    nets = collections.defaultdict(list)
    for ref, p in zip(refs, PARTS):
        sym = SYMBOLS[p.sym]
        numbers = [n for n, _ in sym["left"] + sym["right"]]
        if sorted(numbers) != sorted(p.pins):
            errors.append(f"{ref}: pins {sorted(p.pins)} != symbol pins {sorted(numbers)}")
        for num, net in p.pins.items():
            if net:
                nets[net].append(f"{ref}.{num}")
    for net, members in nets.items():
        if len({m.split('.')[0] for m in members}) < 2:
            errors.append(f"net {net} has a single part: {members}")
    if errors:
        raise SystemExit("netlist errors:\n  " + "\n  ".join(errors))
    return nets

# --------------------------------------------------------------------------
# S-expression output
# --------------------------------------------------------------------------

def num(x):
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def uid(key):
    return str(uuid.uuid5(NS, key))


def font(size=1.27):
    return f"(font (size {num(size)} {num(size)}))"


def sym_geometry(name):
    s = SYMBOLS[name]
    rows = max(len(s["left"]), len(s["right"]), 1)
    half_h = rows * G / 2
    half_w = s["w"] / 2
    pins = []  # (number, name, x, y, angle) in symbol coordinates (Y up)
    for side, items in (("L", s["left"]), ("R", s["right"])):
        for i, (number, pname) in enumerate(items):
            y = ((rows - 1) / 2 - i) * G
            if side == "L":
                pins.append((number, pname, -half_w - G, y, 0))
            else:
                pins.append((number, pname, half_w + G, y, 180))
    return half_w, half_h, pins


def lib_symbol(name, qualified):
    s = SYMBOLS[name]
    half_w, half_h, pins = sym_geometry(name)
    full = f"{LIB}:{name}" if qualified else name
    names = "" if s["names"] else " hide"
    out = [f'(symbol "{full}" (pin_names (offset 0.508){names}) (exclude_from_sim no) (in_bom yes) (on_board yes)',
           f'  (property "Reference" "{PREFIX[name]}" (at 0 {num(half_h + 1.27)} 0) (effects {font()}))',
           f'  (property "Value" "{name}" (at 0 {num(-half_h - 1.27)} 0) (effects {font()}))',
           f'  (property "Footprint" "" (at 0 0 0) (effects {font()} hide))',
           f'  (property "Datasheet" "" (at 0 0 0) (effects {font()} hide))',
           f'  (symbol "{name}_0_1" (rectangle (start {num(-half_w)} {num(half_h)}) (end {num(half_w)} {num(-half_h)})'
           f' (stroke (width 0.254) (type default)) (fill (type background))))',
           f'  (symbol "{name}_1_1"']
    for number, pname, x, y, angle in pins:
        out.append(f'    (pin passive line (at {num(x)} {num(y)} {angle}) (length {num(G)})'
                   f' (name "{pname}" (effects {font()})) (number "{number}" (effects {font()})))')
    out.append("  )")
    out.append(")")
    return "\n".join(out)


def text_width(s):
    return len(s) * 1.1 + 2


def layout(refs):
    """Flow parts into columns, a new column per block. Returns [(x, y)] and block titles."""
    x0, y0, col_w, max_y = 30.48, 40.64, 88.9, 520.0
    positions, titles = [], []
    x, y, block = x0 - col_w, max_y, None
    for p in PARTS:
        _, half_h, _ = sym_geometry(p.sym)
        h = 2 * half_h + 7.62
        if p.block != block or y + h > max_y:
            x += col_w
            y = y0
            if p.block != block:
                titles.append((p.block, x - 20.32, y - 10.16))
            block = p.block
        cy = round((y + half_h + 3.81) / G) * G
        positions.append((x, cy))
        y = cy + half_h + 3.81 + G
    return positions, titles


def schematic(refs):
    used = sorted({p.sym for p in PARTS})
    positions, titles = layout(refs)
    out = [f'(kicad_sch (version 20231120) (generator "gen_kicad") (generator_version "8.0")',
           f'  (uuid "{ROOT_UUID}")',
           '  (paper "A1")',
           '  (title_block (title "STM32F103 PWM fan controller") (rev "1.0")'
           ' (comment 1 "Generated by hardware/tools/gen_kicad.py - edit the script, not this file"))',
           "  (lib_symbols"]
    out += ["    " + line for name in used for line in lib_symbol(name, True).splitlines()]
    out.append("  )")

    for title, x, y in titles:
        out.append(f'  (text "{title}" (exclude_from_sim no) (at {num(x)} {num(y)} 0)'
                   f' (effects {font(3.81)} (justify left bottom)) (uuid "{uid("title" + title)}"))')

    for ref, p, (x, y) in zip(refs, PARTS, positions):
        half_w, half_h, pins = sym_geometry(p.sym)
        dnp = "yes" if p.dnp else "no"
        out.append(f'  (symbol (lib_id "{LIB}:{p.sym}") (at {num(x)} {num(y)} 0) (unit 1)'
                   f' (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp {dnp})')
        out.append(f'    (uuid "{uid(ref)}")')
        props = [("Reference", ref, y - half_h - 1.27, False),
                 ("Value", p.value, y + half_h + 1.27, False),
                 ("Footprint", p.fp, y, True), ("Datasheet", "", y, True),
                 ("MPN", p.mpn, y, True), ("Description", p.desc, y, True)]
        for key, value, py, hide in props:
            h = " hide" if hide else ""
            value = value.replace('"', "'")
            out.append(f'    (property "{key}" "{value}" (at {num(x)} {num(py)} 0) (effects {font()}{h}))')
        for number, _, _, _, _ in pins:
            out.append(f'    (pin "{number}" (uuid "{uid(ref + "." + number)}"))')
        out.append(f'    (instances (project "{PROJECT}" (path "/{ROOT_UUID}" (reference "{ref}") (unit 1))))')
        out.append("  )")

        # Labels at pin ends (schematic Y axis points down)
        for number, _, px, py, angle in pins:
            sx, sy = x + px, y - py
            net = p.pins[number]
            key = f"{ref}.{number}"
            if net is None:
                out.append(f'  (no_connect (at {num(sx)} {num(sy)}) (uuid "{uid("nc" + key)}"))')
                continue
            if angle == 0:  # left pin: text goes left
                out.append(f'  (label "{net}" (at {num(sx)} {num(sy)} 180) (fields_autoplaced yes)'
                           f' (effects {font()} (justify right bottom)) (uuid "{uid("lbl" + key)}"))')
            else:
                out.append(f'  (label "{net}" (at {num(sx)} {num(sy)} 0) (fields_autoplaced yes)'
                           f' (effects {font()} (justify left bottom)) (uuid "{uid("lbl" + key)}"))')

    out.append('  (sheet_instances (path "/" (page "1")))')
    out.append(")")
    return "\n".join(out) + "\n"


def symbol_library():
    body = [lib_symbol(name, False) for name in sorted(SYMBOLS)]
    lines = ['(kicad_symbol_lib (version 20231120) (generator "gen_kicad") (generator_version "8.0")']
    lines += ["  " + line for b in body for line in b.splitlines()]
    lines.append(")")
    return "\n".join(lines) + "\n"


PCB_LAYERS = [
    (0, "F.Cu", "signal"), (31, "B.Cu", "signal"),
    (32, "B.Adhes", "user", "B.Adhesive"), (33, "F.Adhes", "user", "F.Adhesive"),
    (34, "B.Paste", "user"), (35, "F.Paste", "user"),
    (36, "B.SilkS", "user", "B.Silkscreen"), (37, "F.SilkS", "user", "F.Silkscreen"),
    (38, "B.Mask", "user"), (39, "F.Mask", "user"),
    (40, "Dwgs.User", "user", "User.Drawings"), (41, "Cmts.User", "user", "User.Comments"),
    (42, "Eco1.User", "user", "User.Eco1"), (43, "Eco2.User", "user", "User.Eco2"),
    (44, "Edge.Cuts", "user"), (45, "Margin", "user"),
    (46, "B.CrtYd", "user", "B.Courtyard"), (47, "F.CrtYd", "user", "F.Courtyard"),
    (48, "B.Fab", "user"), (49, "F.Fab", "user"),
]


def pcb():
    layers = "\n".join("    (" + " ".join([str(l[0])] + [f'"{v}"' if i != 1 else v for i, v in enumerate(l[1:])])
                       + ")" for l in PCB_LAYERS)
    # Board outline 100 x 80 mm with four M3 holes (drawn as Edge.Cuts circles).
    holes = "\n".join(
        f'  (gr_circle (center {x} {y}) (end {x + 1.6} {y}) (stroke (width 0.05) (type default))'
        f' (fill none) (layer "Edge.Cuts") (uuid "{uid(f"hole{x}{y}")}"))'
        for x, y in ((4, 4), (96, 4), (4, 76), (96, 76)))
    return f"""(kicad_pcb (version 20240108) (generator "gen_kicad") (generator_version "8.0")
  (general (thickness 1.6) (legacy_teardrops no))
  (paper "A4")
  (title_block (title "STM32F103 PWM fan controller") (rev "1.0"))
  (layers
{layers}
  )
  (setup (pad_to_mask_clearance 0))
  (net 0 "")
  (gr_rect (start 0 0) (end 100 80) (stroke (width 0.05) (type default)) (fill none) (layer "Edge.Cuts")
    (uuid "{uid('outline')}"))
{holes}
)
"""


def project():
    netclass_defaults = dict(clearance=0.2, via_diameter=0.6, via_drill=0.3, diff_pair_gap=0.25,
                             diff_pair_via_gap=0.25, diff_pair_width=0.2, microvia_diameter=0.3,
                             microvia_drill=0.1, bus_width=12, wire_width=6, line_style=0, priority=2147483647,
                             pcb_color="rgba(0, 0, 0, 0.000)", schematic_color="rgba(0, 0, 0, 0.000)")
    classes = [dict(netclass_defaults, name="Default", track_width=0.25),
               dict(netclass_defaults, name="Power", track_width=1.0, priority=0)]
    return json.dumps({
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
        "net_settings": {
            "classes": classes,
            "meta": {"version": 3},
            "netclass_patterns": [{"netclass": "Power", "pattern": p}
                                  for p in ("+12V", "VIN_12V", "FAN*_12V", "GND")],
        },
        "board": {"design_settings": {"rules": {"min_clearance": 0.2, "min_track_width": 0.15}}},
        "sheets": [[ROOT_UUID, "Root"]],
    }, indent=2) + "\n"


def bom(refs):
    groups = collections.OrderedDict()
    for ref, p in zip(refs, PARTS):
        key = (p.value, p.fp, p.mpn, p.dnp)
        g = groups.setdefault(key, {"refs": [], "desc": []})
        g["refs"].append(ref)
        if p.desc and p.desc not in g["desc"]:
            g["desc"].append(p.desc)

    def natural(r):
        m = re.match(r"([A-Z]+)(\d+)", r)
        return (m.group(1), int(m.group(2)))

    rows = sorted(groups.items(), key=lambda kv: natural(kv[1]["refs"][0]))
    with open(HW / "bom.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Qty", "References", "Value", "Footprint", "MPN (example)", "Notes", "DNP"])
        for (value, fp, mpn, dnp), g in rows:
            w.writerow([len(g["refs"]), " ".join(sorted(g["refs"], key=natural)), value,
                        fp.split(":")[1], mpn, "; ".join(g["desc"]), "yes" if dnp else ""])
    return len(rows)


def main():
    refs = assign_refs()
    nets = check(refs)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"{PROJECT}.kicad_sch").write_text(schematic(refs))
    (OUT / f"{LIB}.kicad_sym").write_text(symbol_library())
    (OUT / f"{PROJECT}.kicad_pcb").write_text(pcb())
    (OUT / f"{PROJECT}.kicad_pro").write_text(project())
    (OUT / "sym-lib-table").write_text(
        f'(sym_lib_table (version 7)\n  (lib (name "{LIB}") (type "KiCad")'
        f' (uri "${{KIPRJMOD}}/{LIB}.kicad_sym") (options "") (descr "Fan controller symbols"))\n)\n')
    lines = bom(refs)
    print(f"{len(PARTS)} parts, {len(nets)} nets, {lines} BOM lines")


if __name__ == "__main__":
    main()
