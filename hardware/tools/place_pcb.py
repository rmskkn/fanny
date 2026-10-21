#!/usr/bin/env python3
"""
Place the generated netlist on the PCB and export a 3D model.

Runs in KiCad's Python (needs the pcbnew module), for example:

    docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/w -w /w \\
        kicad/kicad:8.0 python3 hardware/tools/place_pcb.py

It loads every footprint in gen_kicad.PARTS, assigns its nets, places it in its
block's area of the 100 x 80 mm board and saves fan_controller.kicad_pcb. The
board is placed, not routed. It then downloads the 3D models it needs into
hardware/kicad/3dmodels/ and writes fan_controller.step and fan_controller.glb.

Run gen_kicad.py first: it rewrites the PCB as a bare outline.
"""

import pathlib
import subprocess
import sys
import urllib.request

import pcbnew

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_kicad as g  # noqa: E402

FP_DIR = pathlib.Path("/usr/share/kicad/footprints")
MODELS = g.OUT / "3dmodels"
MODELS_URL = "https://gitlab.com/kicad/libraries/kicad-packages3D/-/raw/8.0.9/"
# Footprints whose library model is missing: borrow a same-size body.
MODEL_STANDIN = {"Fuse.3dshapes/Fuse_1812_4532Metric.step": "Resistor_SMD.3dshapes/R_1812_4532Metric.step"}
PCB = g.OUT / f"{g.PROJECT}.kicad_pcb"
BOARD_W, BOARD_H, HOLES = g.BOARD_W, g.BOARD_H, g.HOLES
GAP = 0.6

# Connectors lie with their pins along X. Angles are KiCad degrees (counter-clockwise).
ANGLE = {"Conn_01x02": 0, "Conn_01x03": 90, "Conn_01x04": 90, "Fan_4pin": 0,
         "BluePill_A": 270, "BluePill_B": 270}

# Areas (x0, y0, x1, y1) in mm, packed left to right, top to bottom in netlist order.
AREAS = {"Power": (1, 12.6, 21, 30.4), "USB": (1, 31, 20, 40.9), "Test points": (1, 41.3, 20, 44.2),
         "Analog inputs": (20, 33.4, 74.5, 44)}
TERMINAL_X, TERMINAL_Y = 7, 1  # J1, 12 V input, top-left corner clear of the hole
AIN_X, AIN_Y, AIN_PITCH = 20.5, 29, 9.0  # sensor headers, in a row below the Blue Pill
FAN_X, FAN_PITCH = 7, 15.25  # four channels between the bottom mounting holes
for n in range(4):
    AREAS[f"Fan {n + 1}"] = (FAN_X + FAN_PITCH * n, 44.6, FAN_X + FAN_PITCH * (n + 1) - GAP, None)  # bottom: header

# Blue Pill: rows 15.24 mm apart, USB end towards the right board edge. Viewed with
# the USB end up, row A is on the right and row B on the left; turned so USB points
# right, row B is on top.
BLUEPILL_X, BLUEPILL_Y = 21.5, 7


def mm(v):
    return pcbnew.FromMM(v)


def bbox(fp):
    b = fp.GetBoundingBox(False, False)
    return (pcbnew.ToMM(b.GetX()), pcbnew.ToMM(b.GetY()),
            pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))


def move_box_to(fp, x, y):
    """Move fp so its bounding box's top-left corner is at (x, y)."""
    bx, by, _, _ = bbox(fp)
    pos = fp.GetPosition()
    fp.SetPosition(pcbnew.VECTOR2I(pos.x + mm(x - bx), pos.y + mm(y - by)))


def load(board, ref, p, nets):
    lib, name = p.fp.split(":")
    fp = pcbnew.FootprintLoad(str(FP_DIR / f"{lib}.pretty"), name)
    if fp is None:
        raise SystemExit(f"{ref}: footprint {p.fp} not found")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetReference(ref)
    fp.SetValue(p.value)
    fp.SetPath(pcbnew.KIID_PATH("/" + g.uid(ref)))  # links it to its schematic symbol
    fp.SetDNP(p.dnp)
    for pad in fp.Pads():
        net = p.pins.get(pad.GetNumber())
        if net:
            if net not in nets:
                nets[net] = pcbnew.NETINFO_ITEM(board, net)
                board.Add(nets[net])
            pad.SetNet(nets[net])
    models = fp.Models()  # use the STEP files fetch_models() downloads next to the project
    for i in range(models.size()):  # index access: iterating yields copies
        rel = pathlib.Path(models[i].m_Filename.replace("${KICAD8_3DMODEL_DIR}/", "")).with_suffix(".step")
        models[i].m_Filename = "${KIPRJMOD}/3dmodels/" + str(rel)
    fp.SetOrientationDegrees(ANGLE.get(p.sym, 0))
    board.Add(fp)
    return fp


def pack(fps, area):
    x0, y0, x1, y1 = area
    x, y, row_h = x0, y0, 0
    for ref, fp in fps:
        bx, by, bx1, by1 = bbox(fp)
        w, h = bx1 - bx, by1 - by
        if x > x0 and x + w > x1:
            x, y, row_h = x0, y + row_h + GAP, 0
        if x + w > x1 or y + h > y1:
            raise SystemExit(f"{ref} does not fit in area {area}")
        move_box_to(fp, x, y)
        x += w + GAP
        row_h = max(row_h, h)


def place(board):
    refs = g.assign_refs()
    nets = {}
    blocks = {}
    for ref, p in zip(refs, g.PARTS):
        blocks.setdefault(p.block, []).append((ref, p, load(board, ref, p, nets)))

    for block, items in blocks.items():
        if block == "MCU":
            for ref, p, fp in items:
                row = 0 if p.sym == "BluePill_B" else 1
                move_box_to(fp, BLUEPILL_X, BLUEPILL_Y + row * 15.24)
            continue
        if block.startswith("Fan "):
            x0, y0, x1, _ = AREAS[block]
            for ref, p, fp in items:
                if p.sym == "Fan_4pin":  # header on the bottom edge
                    bx, by, bx1, by1 = bbox(fp)
                    top = BOARD_H - 1.5 - (by1 - by)
                    move_box_to(fp, (x0 + x1 - (bx1 - bx)) / 2, top)
            pack([(r, f) for r, p, f in items if p.sym != "Fan_4pin"], (x0, y0, x1, top - GAP))
            continue
        if block == "Power":
            for ref, p, fp in items:
                if p.sym == "Conn_01x02":
                    move_box_to(fp, TERMINAL_X, TERMINAL_Y)
            pack([(r, f) for r, p, f in items if p.sym != "Conn_01x02"], AREAS[block])
            continue
        if block == "Analog inputs":
            for k, (ref, p, fp) in enumerate(i for i in items if i[1].sym == "Conn_01x03"):
                move_box_to(fp, AIN_X + k * AIN_PITCH, AIN_Y)
            pack([(r, f) for r, p, f in items if p.sym != "Conn_01x03"], AREAS[block])
            continue
        pack([(r, f) for r, p, f in items], AREAS[block])
    return [(ref, fp) for items in blocks.values() for ref, _, fp in items]


def check(fps):
    errors = []
    boxes = [(ref, bbox(fp)) for ref, fp in fps]
    for ref, (x, y, x1, y1) in boxes:
        if x < 0.5 or y < 0.5 or x1 > BOARD_W - 0.5 or y1 > BOARD_H - 0.5:
            errors.append(f"{ref} is off the board")
        for hx, hy in HOLES:
            if x - 3 < hx < x1 + 3 and y - 3 < hy < y1 + 3:
                errors.append(f"{ref} is too close to the hole at ({hx}, {hy})")
    for i, (a, (ax, ay, ax1, ay1)) in enumerate(boxes):
        for b, (bx, by, bx1, by1) in boxes[i + 1:]:
            if ax < bx1 and bx < ax1 and ay < by1 and by < ay1:
                errors.append(f"{a} overlaps {b}")
    if errors:
        raise SystemExit("placement errors:\n  " + "\n  ".join(errors))


def fetch_models(fps):
    MODELS.mkdir(exist_ok=True)
    missing = set()
    for ref, fp in fps:
        for model in fp.Models():
            rel = model.m_Filename.replace("${KIPRJMOD}/3dmodels/", "")
            step = MODELS / rel
            if step.exists() or rel in missing:
                continue
            step.parent.mkdir(parents=True, exist_ok=True)
            try:
                urllib.request.urlretrieve(MODELS_URL + urllib.request.quote(MODEL_STANDIN.get(rel, rel)), step)
            except urllib.error.HTTPError as e:
                missing.add(rel)
                print(f"warning: no 3D model for {ref} ({rel}): HTTP {e.code}")


def export_3d():
    for fmt in ("step", "glb"):
        run = subprocess.run(["kicad-cli", "pcb", "export", fmt, "--no-dnp", "--force",
                              "-o", str(g.OUT / f"{g.PROJECT}.{fmt}"), str(PCB)],
                             capture_output=True, text=True, check=True)
        failed = [line for line in (run.stdout + run.stderr).splitlines() if line.startswith("Could not add")]
        if failed:
            raise SystemExit(f"{fmt} export is missing {len(failed)} models, first: {failed[0]}")


def main():
    board = pcbnew.LoadBoard(str(PCB))
    if len(board.GetFootprints()):
        raise SystemExit(f"{PCB.name} already has footprints; run gen_kicad.py first")
    fps = place(board)
    check(fps)
    board.Save(str(PCB))
    fetch_models(fps)
    export_3d()
    print(f"placed {len(fps)} footprints; wrote {PCB.name}, {g.PROJECT}.step, {g.PROJECT}.glb")


if __name__ == "__main__":
    main()
