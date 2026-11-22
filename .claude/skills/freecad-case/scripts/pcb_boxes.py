"""Print footprint and through-hole pad-row bounding boxes (KiCad mm) for case constants.

Usage: python3 pcb_boxes.py [REF ...]   (default: every J* and MH* footprint)
"""
import os
import sys

import pcbnew

BOARD = os.path.join(os.path.dirname(__file__), "../../../../hardware/kicad/fan_controller.kicad_pcb")


def box(b):
    mm = pcbnew.ToMM
    return tuple(round(v, 2) for v in (mm(b.GetX()), mm(b.GetY()), mm(b.GetRight()), mm(b.GetBottom())))


board = pcbnew.LoadBoard(os.path.normpath(BOARD))
wanted = set(sys.argv[1:])
for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
    ref = fp.GetReference()
    if wanted and ref not in wanted or not wanted and not ref.startswith(("J", "MH")):
        continue
    pos = fp.GetPosition()
    line = f"{ref:5} at ({pcbnew.ToMM(pos.x):.2f}, {pcbnew.ToMM(pos.y):.2f}) rot {fp.GetOrientationDegrees():g}" \
           f"  footprint {box(fp.GetBoundingBox(False))}"
    pads = [p for p in fp.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    if pads:
        b = pads[0].GetBoundingBox()
        for p in pads[1:]:
            b.Merge(p.GetBoundingBox())
        line += f"  tht_pads {box(b)}"
    print(line)
