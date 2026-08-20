#!/usr/bin/env python3
"""Clean TPS61088 power sheet for intercom — readable layout, correct boost topology."""

from __future__ import annotations

import uuid
from pathlib import Path

ROOT = Path("/Users/tyler/Documents/intercom")
POWER = ROOT / "power.kicad_sch"
MIRROR = Path("/Users/tyler/dev/alfred/hardware/power.kicad_sch")
SYM_LIB = ROOT / "intercom.kicad_sym"
KICAD = Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols")

POWER_UUID = "c1d4a2b8-6e9f-4c3a-8d7b-1a2e3f4c5d60"
pwr_n = 300  # unique #PWR refs


def uid() -> str:
    return str(uuid.uuid4())


def extract(lib: str, name: str, lib_id: str) -> str:
    text = (KICAD / lib).read_text()
    start = text.find(f'\t(symbol "{name}"')
    if start < 0:
        raise SystemExit(f"missing {name}")
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                block = text[start : i + 1]
                return block.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)
    raise SystemExit(f"unclosed {name}")


def prop(name: str, value: str, x: float, y: float, hide: bool = False) -> str:
    h = "\n\t\t\t(hide yes)" if hide else ""
    return f"""\t\t(property "{name}" "{value}"
\t\t\t(at {x:.2f} {y:.2f} 0){h}
\t\t\t(effects (font (size 1.27 1.27)))
\t\t)"""


def pin_def(ptype: str, name: str, number: str, x: float, y: float, rot: int) -> str:
    return f"""\t\t\t(pin {ptype} line
\t\t\t\t(at {x} {y} {rot})
\t\t\t\t(length 2.54)
\t\t\t\t(name "{name}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "{number}" (effects (font (size 1.27 1.27))))
\t\t\t)"""


def tps_symbol() -> str:
    pins = "\n".join(
        [
            # Left
            pin_def("passive", "VIN", "9", -12.7, 7.62, 0),
            pin_def("input", "EN", "2", -12.7, 5.08, 0),
            pin_def("passive", "SW", "4", -12.7, 0.00, 0),
            pin_def("passive", "BOOT", "8", -12.7, -2.54, 0),
            pin_def("passive", "FSW", "3", -12.7, -5.08, 0),
            pin_def("input", "MODE", "13", -12.7, -7.62, 0),
            # Right
            pin_def("passive", "VOUT", "14", 12.7, 7.62, 180),
            pin_def("input", "FB", "17", 12.7, 5.08, 180),
            pin_def("passive", "COMP", "18", 12.7, 2.54, 180),
            pin_def("passive", "ILIM", "19", 12.7, 0.00, 180),
            pin_def("passive", "VCC", "1", 12.7, -2.54, 180),
            pin_def("passive", "SS", "10", 12.7, -5.08, 180),
            pin_def("passive", "NC", "11", 12.7, -7.62, 180),
            pin_def("passive", "NC", "12", 12.7, -10.16, 180),
            # Bottom
            pin_def("power_in", "AGND", "20", -2.54, -12.7, 90),
            pin_def("power_in", "PGND", "21", 2.54, -12.7, 90),
        ]
    )
    return f"""\t(symbol "intercom:TPS61088"
\t\t(exclude_from_sim no)(in_bom yes)(on_board yes)(in_pos_files yes)
{prop("Reference", "U", -7.62, 13.97)}
{prop("Value", "TPS61088", 2.54, 13.97)}
{prop("Footprint", "Package_DFN_QFN:Texas_VQFN-RHL-20_ThermalVias", 0, 0, True)}
{prop("Datasheet", "https://www.ti.com/lit/ds/symlink/tps61088.pdf", 0, 0, True)}
{prop("Description", "10A sync boost", 0, 0, True)}
\t\t(symbol "TPS61088_0_1"
\t\t\t(rectangle (start -10.16 12.7) (end 10.16 -10.16)
\t\t\t\t(stroke (width 0.254) (type default)) (fill (type background)))
\t\t)
\t\t(symbol "TPS61088_1_1"
{pins}
\t\t)
\t)"""


def place(
    lib_id: str,
    ref: str,
    value: str,
    x: float,
    y: float,
    rot: int,
    pins: list[str],
    fp: str = "",
    hide_ref: bool = False,
) -> str:
    pin_blk = "\n".join(f'\t\t(pin "{n}" (uuid "{uid()}"))' for n in pins)
    # label offsets — keep text away from body
    rx, ry = x + 2.54, y - 5.08
    vx, vy = x + 2.54, y - 2.54
    if rot in (90, 270):
        rx, ry = x + 2.54, y - 6.35
        vx, vy = x + 2.54, y - 3.81
    return f"""\t(symbol
\t\t(lib_id "{lib_id}")
\t\t(at {x:.2f} {y:.2f} {rot})
\t\t(unit 1)(body_style 1)
\t\t(exclude_from_sim no)(in_bom yes)(on_board yes)(in_pos_files yes)(dnp no)
\t\t(uuid "{uid()}")
{prop("Reference", ref, rx, ry, hide_ref)}
{prop("Value", value, vx, vy, hide_ref and ref.startswith("#"))}
{prop("Footprint", fp, x, y, True)}
{prop("Datasheet", "", x, y, True)}
{prop("Description", "", x, y, True)}
{pin_blk}
\t\t(instances (project "intercom" (path "/{POWER_UUID}" (reference "{ref}") (unit 1))))
\t)"""


def gnd(x: float, y: float) -> str:
    global pwr_n
    pwr_n += 1
    return place("power:GND", f"#PWR{pwr_n}", "GND", x, y, 0, ["1"], hide_ref=True)


def p5v(x: float, y: float) -> str:
    global pwr_n
    pwr_n += 1
    return place("power:+5V", f"#PWR{pwr_n}", "+5V", x, y, 0, ["1"], hide_ref=True)


def wire(a: tuple[float, float], b: tuple[float, float]) -> str:
    return f"""\t(wire
\t\t(pts (xy {a[0]:.2f} {a[1]:.2f}) (xy {b[0]:.2f} {b[1]:.2f}))
\t\t(stroke (width 0) (type default))
\t\t(uuid "{uid()}")
\t)"""


def junction(p: tuple[float, float]) -> str:
    return f"""\t(junction (at {p[0]:.2f} {p[1]:.2f}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))"""


def label(text: str, p: tuple[float, float], rot: int = 0) -> str:
    return f"""\t(label "{text}" (at {p[0]:.2f} {p[1]:.2f} {rot}) (effects (font (size 1.27 1.27))) (uuid "{uid()}"))"""


def noconnect(p: tuple[float, float]) -> str:
    return f"""\t(no_connect (at {p[0]:.2f} {p[1]:.2f}) (uuid "{uid()}"))"""


def text(body: str, x: float, y: float) -> str:
    return f"""\t(text "{body}" (exclude_from_sim no) (at {x:.2f} {y:.2f} 0)
\t\t(effects (font (size 1.27 1.27)) (justify left top)) (uuid "{uid()}"))"""


def build() -> str:
    o: list[str] = []

    # ========== Grid ==========
    # Battery / switch
    bx, by = 30.48, 100.33
    sx, sy = 55.88, 105.41  # SW aligned with BAT+

    # VBAT vertical bus
    vbat_x = 76.20
    vin_y = 93.98  # VIN / EN height

    # Inductor
    lx, ly = 95.25, 100.33

    # IC
    ux, uy = 127.00, 100.33
    # Pin absolute coords
    VIN = (ux - 12.70, uy + 7.62)
    EN = (ux - 12.70, uy + 5.08)
    SW = (ux - 12.70, uy + 0.00)
    BOOT = (ux - 12.70, uy - 2.54)
    FSW = (ux - 12.70, uy - 5.08)
    MODE = (ux - 12.70, uy - 7.62)
    VOUT = (ux + 12.70, uy + 7.62)
    FB = (ux + 12.70, uy + 5.08)
    COMP = (ux + 12.70, uy + 2.54)
    ILIM = (ux + 12.70, uy + 0.00)
    VCC = (ux + 12.70, uy - 2.54)
    SS = (ux + 12.70, uy - 5.08)
    NC11 = (ux + 12.70, uy - 7.62)
    NC12 = (ux + 12.70, uy - 10.16)
    AGND = (ux - 2.54, uy - 12.70)
    PGND = (ux + 2.54, uy - 12.70)

    # Right-side column for feedback / output (well clear of IC)
    right = 165.10
    out_x = 210.82

    # ----- Places -----
    # Device:Battery + at (0,+5.08), - at (0,-5.08)
    o.append(place("Device:Battery", "BT2", "1S LiPo", bx, by, 0, ["1", "2"]))
    bat_p = (bx, by + 5.08)
    bat_m = (bx, by - 5.08)

    # SW_SPST A (-5.08,0) B (+5.08,0)
    o.append(place("Switch:SW_SPST", "SW2", "POWER", sx, sy, 0, ["1", "2"]))
    sw_a = (sx - 5.08, sy)
    sw_b = (sx + 5.08, sy)

    o.append(
        place(
            "intercom:TPS61088",
            "U2",
            "TPS61088",
            ux,
            uy,
            0,
            ["1", "2", "3", "4", "8", "9", "10", "11", "12", "13", "14", "17", "18", "19", "20", "21"],
            "Package_DFN_QFN:Texas_VQFN-RHL-20_ThermalVias",
        )
    )

    # L horizontal rot90: pin1 (-3.81,0) pin2 (3.81,0)
    o.append(place("Device:L", "L2", "1.2uH", lx, ly, 90, ["1", "2"]))
    l_l = (lx - 3.81, ly)
    l_r = (lx + 3.81, ly)

    # ========== Battery → switch → VBAT ==========
    o += [
        wire(bat_p, (bat_p[0], sy)),
        wire((bat_p[0], sy), sw_a),
        wire(sw_b, (vbat_x, sy)),
        label("VBAT", (vbat_x + 2.54, sy)),
        gnd(bat_m[0], bat_m[1] - 7.62),
        wire(bat_m, (bat_m[0], bat_m[1] - 7.62)),
    ]

    # VBAT vertical spine
    o += [
        wire((vbat_x, sy), (vbat_x, ly)),  # down to inductor height
        junction((vbat_x, sy)),
    ]

    # Input caps on VBAT (below the switch rail, spaced)
    for ref, val, cx in [("C30", "10uF", 81.28), ("C31", "0.1uF", 88.90)]:
        cy = 120.65
        o.append(place("Device:C", ref, val, cx, cy, 0, ["1", "2"]))
        # C: pin1 (0,+3.81) pin2 (0,-3.81)
        top, bot = (cx, cy + 3.81), (cx, cy - 3.81)
        o += [
            wire((vbat_x, sy), (cx, sy)),
            junction((vbat_x, sy)),
            wire((cx, sy), top),
            gnd(cx, bot[1] - 7.62),
            wire(bot, (cx, bot[1] - 7.62)),
        ]

    # ========== Inductor VBAT → SW ==========
    o += [
        wire((vbat_x, ly), l_l),
        wire(l_r, SW),
        junction((vbat_x, ly)),
    ]

    # ========== VIN / EN from VBAT ==========
    o += [
        wire((vbat_x, ly), (vbat_x, VIN[1])),
        wire((vbat_x, VIN[1]), VIN),
        junction((vbat_x, ly)),
        # EN tied to VIN
        wire(EN, (EN[0], VIN[1])),
        wire((EN[0], VIN[1]), VIN),
        junction(VIN),
    ]

    # ========== BOOT 0.1uF between BOOT and SW (left of IC, clear) ==========
    cboot_x = 109.22
    o.append(place("Device:C", "C22", "0.1uF", cboot_x, uy - 1.27, 0, ["1", "2"]))
    ct, cb = (cboot_x, uy - 1.27 + 3.81), (cboot_x, uy - 1.27 - 3.81)
    # Connect BOOT (higher) to top of cap, SW to bottom via horizontal stubs
    o += [
        wire(BOOT, (cboot_x, BOOT[1])),
        wire((cboot_x, BOOT[1]), ct),
        wire(cb, (cboot_x, SW[1])),
        wire((cboot_x, SW[1]), SW),
        junction(SW),
    ]

    # ========== FSW resistor 200k between FSW and SW ==========
    rfreq_x = 101.60
    o.append(place("Device:R", "R20", "200k", rfreq_x, uy - 2.54, 0, ["1", "2"]))
    rt, rb = (rfreq_x, uy - 2.54 + 3.81), (rfreq_x, uy - 2.54 - 3.81)
    o += [
        wire(FSW, (rfreq_x, FSW[1])),
        wire((rfreq_x, FSW[1]), rt),
        wire(rb, (rfreq_x, SW[1])),
        wire((rfreq_x, SW[1]), SW),
        junction(SW),
    ]

    # ========== MODE float ==========
    o.append(noconnect(MODE))

    # ========== AGND / PGND ==========
    gnd_y = 127.00
    o.append(gnd(ux, gnd_y))
    o += [
        wire(AGND, (AGND[0], gnd_y)),
        wire(PGND, (PGND[0], gnd_y)),
        wire((AGND[0], gnd_y), (PGND[0], gnd_y)),
        wire((ux, gnd_y), (AGND[0], gnd_y)),
        junction((AGND[0], gnd_y)),
        junction((PGND[0], gnd_y)),
        junction((ux, gnd_y)),
    ]

    # NC11/NC12 → GND (datasheet: for thermal)
    o += [
        wire(NC11, (NC11[0] + 5.08, NC11[1])),
        wire((NC11[0] + 5.08, NC11[1]), (NC11[0] + 5.08, gnd_y)),
        wire((NC11[0] + 5.08, gnd_y), (ux, gnd_y)),
        junction((ux, gnd_y)),
        wire(NC12, (NC12[0] + 7.62, NC12[1])),
        wire((NC12[0] + 7.62, NC12[1]), (NC12[0] + 7.62, gnd_y)),
        wire((NC12[0] + 7.62, gnd_y), (ux, gnd_y)),
        junction((ux, gnd_y)),
    ]

    # ========== VCC 1uF ==========
    o.append(place("Device:C", "C23", "1uF", right, VCC[1], 0, ["1", "2"]))
    t, b = (right, VCC[1] + 3.81), (right, VCC[1] - 3.81)
    o += [
        wire(VCC, (right, VCC[1])),
        # attach to nearer pin
        wire((right, VCC[1]), t if abs(t[1] - VCC[1]) < abs(b[1] - VCC[1]) else b),
        gnd(right, b[1] - 7.62 if abs(t[1] - VCC[1]) < abs(b[1] - VCC[1]) else t[1] + 7.62),
    ]
    # fix VCC cap GND more carefully
    o = o[:-1]  # remove last gnd attempt
    near = t if abs(t[1] - VCC[1]) <= abs(b[1] - VCC[1]) else b
    far = b if near is t else t
    o += [
        wire((right, VCC[1]), near),
        gnd(right, far[1] - 7.62 if far[1] < near[1] else far[1] + 7.62),
        wire(far, (right, far[1] - 7.62 if far[1] < near[1] else far[1] + 7.62)),
    ]

    # ========== SS 47nF ==========
    ss_x = right + 12.70
    o.append(place("Device:C", "C24", "47nF", ss_x, SS[1], 0, ["1", "2"]))
    t, b = (ss_x, SS[1] + 3.81), (ss_x, SS[1] - 3.81)
    near = t if abs(t[1] - SS[1]) <= abs(b[1] - SS[1]) else b
    far = b if near is t else t
    o += [
        wire(SS, (ss_x, SS[1])),
        wire((ss_x, SS[1]), near),
        gnd(ss_x, far[1] - 7.62 if far[1] < near[1] else far[1] + 7.62),
        wire(far, (ss_x, far[1] - 7.62 if far[1] < near[1] else far[1] + 7.62)),
    ]

    # ========== ILIM 200k ==========
    ilim_x = right
    o.append(place("Device:R", "R21", "200k", ilim_x, ILIM[1] + 12.70, 0, ["1", "2"]))
    # Place R below ILIM pin area — actually put to the right at ILIM height with vertical R
    # Redo: R vertical at (right, ILIM[1]) won't work if VCC is there.
    # Put ILIM resistor further right
    ilim_x = right + 25.40
    o[-1] = place("Device:R", "R21", "200k", ilim_x, ILIM[1], 0, ["1", "2"])  # replace last? messy

    # Clear approach: rebuild right-side from scratch in a cleaner second pass
    # Actually let me just finish carefully without the botched VCC section

    # Wipe and rebuild file with a simpler, proven layout function
    return None  # signal rewrite


def pin0(cx: float, cy: float, px: float, py: float) -> tuple[float, float]:
    """Absolute pin position for a rot=0 symbol. KiCad: y' = cy - py."""
    return (cx + px, cy - py)


def pins_rc(cx: float, cy: float) -> tuple[tuple[float, float], tuple[float, float]]:
    """Device:R / Device:C vertical: pin1 (upper), pin2 (lower)."""
    return pin0(cx, cy, 0, 3.81), pin0(cx, cy, 0, -3.81)


def pins_l90(cx: float, cy: float) -> tuple[tuple[float, float], tuple[float, float]]:
    """Device:L / Device:R at 90°: pin1 (right), pin2 (left)."""
    # θ=90: x' = cx + py, y' = cy + px
    return (cx + 3.81, cy), (cx - 3.81, cy)


def build_clean() -> str:
    """Fully clean sheet — correct KiCad pin math, 1.27 mm grid."""
    global pwr_n
    pwr_n = 400
    o: list[str] = []

    # IC center (on-grid)
    ux, uy = 129.54, 95.25
    # Symbol pin locals → absolute (y' = uy - py)
    VIN = pin0(ux, uy, -12.70, 7.62)
    EN = pin0(ux, uy, -12.70, 5.08)
    SW = pin0(ux, uy, -12.70, 0.00)
    BOOT = pin0(ux, uy, -12.70, -2.54)
    FSW = pin0(ux, uy, -12.70, -5.08)
    MODE = pin0(ux, uy, -12.70, -7.62)
    VOUT = pin0(ux, uy, 12.70, 7.62)
    FB = pin0(ux, uy, 12.70, 5.08)
    COMP = pin0(ux, uy, 12.70, 2.54)
    ILIM = pin0(ux, uy, 12.70, 0.00)
    VCC = pin0(ux, uy, 12.70, -2.54)
    SS = pin0(ux, uy, 12.70, -5.08)
    NC11 = pin0(ux, uy, 12.70, -7.62)
    NC12 = pin0(ux, uy, 12.70, -10.16)
    AGND = pin0(ux, uy, -2.54, -12.70)
    PGND = pin0(ux, uy, 2.54, -12.70)

    # ---- Left: battery + switch ----
    bx, by = 25.40, 95.25
    o.append(place("Device:Battery", "BT2", "1S LiPo", bx, by, 0, ["1", "2"]))
    bat_p = pin0(bx, by, 0, 5.08)   # +
    bat_m = pin0(bx, by, 0, -5.08)  # −

    sx, sy = 50.80, bat_p[1]
    o.append(place("Switch:SW_SPST", "SW2", "POWER", sx, sy, 0, ["1", "2"]))
    sw_a, sw_b = (sx - 5.08, sy), (sx + 5.08, sy)

    vbat_x = 71.12
    o += [
        wire(bat_p, sw_a),
        wire(sw_b, (vbat_x, sy)),
        label("VBAT", (vbat_x + 2.54, sy)),
        place("power:PWR_FLAG", "#FLGVBAT", "PWR_FLAG", vbat_x, sy - 10.16, 0, ["1"], hide_ref=True),
        wire((vbat_x, sy), (vbat_x, sy - 10.16)),
        junction((vbat_x, sy)),
        gnd(bx, bat_m[1] + 10.16),
        place("power:PWR_FLAG", "#FLGGND", "PWR_FLAG", bx + 10.16, bat_m[1] + 10.16, 0, ["1"], hide_ref=True),
        wire((bx, bat_m[1] + 10.16), (bx + 10.16, bat_m[1] + 10.16)),
        wire(bat_m, (bx, bat_m[1] + 10.16)),
        junction((bx, bat_m[1] + 10.16)),
    ]

    # Input caps hanging below VBAT (larger Y)
    for ref, val, cx in [("C30", "10uF", 76.20), ("C31", "0.1uF", 86.36)]:
        cy = sy + 15.24
        o.append(place("Device:C", ref, val, cx, cy, 0, ["1", "2"]))
        p1, p2 = pins_rc(cx, cy)  # p1 upper (toward rail), p2 lower (GND)
        o += [
            wire((vbat_x, sy), (cx, sy)),
            junction((vbat_x, sy)),
            wire((cx, sy), p1),
            gnd(cx, p2[1] + 7.62),
            wire(p2, (cx, p2[1] + 7.62)),
        ]

    # ---- IC ----
    o.append(
        place(
            "intercom:TPS61088",
            "U2",
            "TPS61088",
            ux,
            uy,
            0,
            ["1", "2", "3", "4", "8", "9", "10", "11", "12", "13", "14", "17", "18", "19", "20", "21"],
            "Package_DFN_QFN:Texas_VQFN-RHL-20_ThermalVias",
        )
    )

    # ---- Inductor VBAT → SW (horizontal) ----
    lx, ly = 99.06, SW[1]
    o.append(place("Device:L", "L2", "1.2uH", lx, ly, 90, ["1", "2"]))
    l_r, l_l = pins_l90(lx, ly)  # pin1 right, pin2 left

    o += [
        wire((vbat_x, sy), (vbat_x, ly)),
        junction((vbat_x, sy)),
        wire((vbat_x, ly), l_l),
        wire(l_r, SW),
        junction((vbat_x, ly)),
        # VIN from VBAT spine
        wire((vbat_x, ly), (vbat_x, VIN[1])),
        wire((vbat_x, VIN[1]), VIN),
        junction((vbat_x, VIN[1])),
        # EN tied to VIN
        wire(EN, (EN[0] - 2.54, EN[1])),
        wire((EN[0] - 2.54, EN[1]), (EN[0] - 2.54, VIN[1])),
        wire((EN[0] - 2.54, VIN[1]), VIN),
        junction(VIN),
    ]

    # BOOT–C22–SW (cap between BOOT and SW vertically, left of IC)
    cx = 111.76
    cy = (BOOT[1] + SW[1]) / 2
    o.append(place("Device:C", "C22", "0.1uF", cx, cy, 0, ["1", "2"]))
    p1, p2 = pins_rc(cx, cy)
    # BOOT is below SW (BOOT y larger). Upper pin → SW, lower → BOOT
    o += [
        wire(SW, (cx, SW[1])),
        junction(SW),
        wire((cx, SW[1]), p1),
        wire(p2, (cx, BOOT[1])),
        wire((cx, BOOT[1]), BOOT),
    ]

    # FSW–R20–SW
    rx = 104.14
    ry = (FSW[1] + SW[1]) / 2
    o.append(place("Device:R", "R20", "200k", rx, ry, 0, ["1", "2"]))
    p1, p2 = pins_rc(rx, ry)
    o += [
        wire(SW, (rx, SW[1])),
        junction(SW),
        wire((rx, SW[1]), p1),
        wire(p2, (rx, FSW[1])),
        wire((rx, FSW[1]), FSW),
    ]

    o.append(noconnect(MODE))

    # Ground under IC
    gy = AGND[1] + 7.62
    o.append(gnd(ux, gy))
    o += [
        wire(AGND, (AGND[0], gy)),
        wire(PGND, (PGND[0], gy)),
        wire((AGND[0], gy), (PGND[0], gy)),
        wire((ux, gy), (AGND[0], gy)),
        junction((AGND[0], gy)),
        junction((PGND[0], gy)),
        junction((ux, gy)),
    ]
    for p, ox in [(NC11, 5.08), (NC12, 10.16)]:
        o += [
            wire(p, (p[0] + ox, p[1])),
            wire((p[0] + ox, p[1]), (p[0] + ox, gy)),
            wire((p[0] + ox, gy), (ux, gy)),
            junction((ux, gy)),
        ]

    def cap_below_pin(ref: str, val: str, pin: tuple[float, float], cx: float):
        """Cap hanging below a right-side pin: pin → p1, p2 → GND."""
        nonlocal o
        cy = pin[1] + 3.81  # p1 lands on pin.y
        o.append(place("Device:C", ref, val, cx, cy, 0, ["1", "2"]))
        p1, p2 = pins_rc(cx, cy)
        o += [
            wire(pin, p1),
            gnd(cx, p2[1] + 7.62),
            wire(p2, (cx, p2[1] + 7.62)),
        ]

    def r_below_pin(ref: str, val: str, pin: tuple[float, float], cx: float):
        nonlocal o
        cy = pin[1] + 3.81
        o.append(place("Device:R", ref, val, cx, cy, 0, ["1", "2"]))
        p1, p2 = pins_rc(cx, cy)
        o += [
            wire(pin, p1),
            gnd(cx, p2[1] + 7.62),
            wire(p2, (cx, p2[1] + 7.62)),
        ]

    # Right-side: VCC, SS, ILIM hanging down (staggered X)
    cap_below_pin("C23", "1uF", VCC, 157.48)
    cap_below_pin("C24", "47nF", SS, 170.18)
    r_below_pin("R21", "200k", ILIM, 210.82)

    # COMP network to the right of COMP, below pin height clutter
    # COMP — R24 (horiz) — mid — C25 (vert to GND)
    #                |
    #               C26 (vert to GND) from COMP
    rx = 152.40
    o.append(place("Device:R", "R24", "10k", rx, COMP[1], 90, ["1", "2"]))
    r_r, r_l = pins_l90(rx, COMP[1])  # right, left
    mid = (r_r[0] + 5.08, COMP[1])
    o += [wire(COMP, r_l), wire(r_r, mid)]

    cx = mid[0] + 7.62
    cy = COMP[1] + 3.81
    o.append(place("Device:C", "C25", "4.7nF", cx, cy, 0, ["1", "2"]))
    p1, p2 = pins_rc(cx, cy)
    o += [
        wire(mid, p1),
        junction(mid),
        gnd(cx, p2[1] + 7.62),
        wire(p2, (cx, p2[1] + 7.62)),
    ]

    cx = COMP[0] + 5.08
    cy = COMP[1] + 12.70
    o.append(place("Device:C", "C26", "47pF", cx, cy, 0, ["1", "2"]))
    p1, p2 = pins_rc(cx, cy)
    o += [
        wire(COMP, (cx, COMP[1])),
        junction(COMP),
        wire((cx, COMP[1]), p1),
        gnd(cx, p2[1] + 7.62),
        wire(p2, (cx, p2[1] + 7.62)),
    ]

    # ---- VOUT / +5V rail ----
    out_y = VOUT[1]
    rail_x0 = VOUT[0]
    rail_x1 = 220.98
    pwr_y = out_y - 7.62
    o += [
        wire(VOUT, (rail_x1, out_y)),
        label("+5V", (rail_x1 - 10.16, out_y)),
        p5v(rail_x1, pwr_y),
        wire((rail_x1, out_y), (rail_x1, pwr_y)),
        place("power:PWR_FLAG", "#FLG5V", "PWR_FLAG", rail_x1 + 12.70, pwr_y, 0, ["1"], hide_ref=True),
        wire((rail_x1, pwr_y), (rail_x1 + 12.70, pwr_y)),
        junction((rail_x1, pwr_y)),
        junction(VOUT),
    ]

    # Output ceramics below +5V rail
    for i, cx in enumerate([157.48, 170.18, 182.88]):
        cy = out_y + 12.70
        ref = f"C{27 + i}"
        o.append(place("Device:C", ref, "22uF", cx, cy, 0, ["1", "2"]))
        p1, p2 = pins_rc(cx, cy)
        o += [
            wire((rail_x0, out_y), (cx, out_y)),
            junction((cx, out_y)),
            wire((cx, out_y), p1),
            gnd(cx, p2[1] + 7.62),
            wire(p2, (cx, p2[1] + 7.62)),
        ]

    # FB divider: +5V — R22 — mid — R23 — GND, FB taps mid
    fb_x = 195.58
    r22_cy = out_y + 3.81
    o.append(place("Device:R", "R22", "178k", fb_x, r22_cy, 0, ["1", "2"]))
    p1, p2 = pins_rc(fb_x, r22_cy)  # p1 on rail, p2 = mid node
    fb_node = p2
    o += [
        wire((rail_x0, out_y), (fb_x, out_y)),
        junction((fb_x, out_y)),
        wire((fb_x, out_y), p1),
        wire(FB, (fb_x, FB[1])),
        wire((fb_x, FB[1]), fb_node),
        junction(fb_node),
    ]
    r23_cy = fb_node[1] + 3.81
    o.append(place("Device:R", "R23", "56k", fb_x, r23_cy, 0, ["1", "2"]))
    t3, b3 = pins_rc(fb_x, r23_cy)
    o += [
        wire(fb_node, t3),
        gnd(fb_x, b3[1] + 7.62),
        wire(b3, (fb_x, b3[1] + 7.62)),
    ]

    o.append(
        text(
            "TPS61088 — 1S LiPo to +5V\\n"
            "SW2 on BAT+  |  EN=VBAT  |  MODE open (PFM)\\n"
            "R22/R23 178k/56k = 5.0V\\n"
            "R21 200k ~6A ILIM  |  R20 200k FSW\\n"
            "PCB: short SW pads 4-7, VOUT 14-16\\n"
            "Local amp decoupling stays on root sheet",
            20.32,
            25.40,
        )
    )

    libs = "\n".join(
        [
            tps_symbol(),
            extract("power.kicad_sym", "+5V", "power:+5V"),
            extract("power.kicad_sym", "GND", "power:GND"),
            extract("power.kicad_sym", "PWR_FLAG", "power:PWR_FLAG"),
            extract("Device.kicad_sym", "Battery", "Device:Battery"),
            extract("Device.kicad_sym", "R", "Device:R"),
            extract("Device.kicad_sym", "C", "Device:C"),
            extract("Device.kicad_sym", "L", "Device:L"),
            extract("Switch.kicad_sym", "SW_SPST", "Switch:SW_SPST"),
        ]
    )

    return f"""(kicad_sch
\t(version 20260306)
\t(generator "eeschema")
\t(generator_version "10.0")
\t(uuid "{POWER_UUID}")
\t(paper "A4")
\t(title_block
\t\t(title "Intercom power")
\t\t(comment 1 "TPS61088 1S LiPo to +5V")
\t)
\t(lib_symbols
{libs}
\t)
{chr(10).join(o)}
\t(sheet_instances
\t\t(path "/" (page "2"))
\t)
\t(embedded_fonts no)
)
"""


def main():
    sch = build_clean()
    MIRROR.parent.mkdir(parents=True, exist_ok=True)
    MIRROR.write_text(sch)
    print(f"Wrote {MIRROR} ({len(sch.splitlines())} lines)")
    try:
        POWER.write_text(sch)
        print(f"Wrote {POWER}")
    except OSError as e:
        print(f"Could not write {POWER}: {e}")
        print("Close the sheet in KiCad, then copy from alfred/hardware/")
    # keep symbol lib in sync (best-effort)
    try:
        SYM_LIB.write_text(
            f"""(kicad_symbol_lib
\t(version 20260306)
\t(generator "kicad_symbol_editor")
\t(generator_version "10.0")
{tps_symbol().replace('(symbol "intercom:TPS61088"', '(symbol "TPS61088"', 1)}
)
"""
        )
        # mirror symbol into alfred/hardware too
        (MIRROR.parent / "intercom.kicad_sym").write_text(SYM_LIB.read_text())
    except OSError as e:
        print(f"Could not write symbol lib: {e}")
    # validate against workspace copy
    import subprocess

    r = subprocess.run(
        [
            "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
            "sch",
            "export",
            "svg",
            "--output",
            "/tmp/power-clean",
            str(MIRROR),
        ],
        capture_output=True,
        text=True,
    )
    print(r.stdout)
    print(r.stderr[-500:] if r.stderr else "")
    print("exit", r.returncode)


if __name__ == "__main__":
    main()
