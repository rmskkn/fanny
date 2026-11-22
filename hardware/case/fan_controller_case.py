# SPDX-License-Identifier: GPL-3.0-only
"""Two-part printed case for the fan controller PCB (hardware/kicad/fan_controller.kicad_pcb).

tray:  low open tray. The PCB screws onto 4 standoffs. The wall rises only
       2 mm above the PCB, so the tray also works on its own with no cover.
cover: plate with a skirt that sits on the tray wall. A lip goes inside the
       tray wall and holds it by friction. The four fan headers are the
       horizontal, right-angle kind (same pads as the vertical ones the
       .kicad_pcb still shows, different shroud), so their cable leaves
       sideways through a slot cut into the front wall, shared by the tray
       and the cover skirt, instead of a hole in the top. The JST PH sensor
       inputs and the 12 V Mate-N-Lok input each get a plain rectangular
       window through the top plate, and a hole in the right wall leaves
       the micro-B USB free.

Build (headless): writes fan_controller_case.FCStd, the STEP files and the
print-ready STL files next to this script.
    freecadcmd fan_controller_case.py
Build and render the README images (opens the FreeCAD window briefly):
    freecad fan_controller_case.py

Print: no supports. Tray floor down. The cover STL is flipped, top face down,
so the windows are plain holes and the USB notch opens upward. 0.2 mm
layers, 2 perimeters, 15 % infill. PLA or PETG.
Fasten: 4x M2x6 countersunk screws (DIN 965) from under the tray, through the
PCB, with an M2 nut on the PCB top. Fit MH4 before plugging in the Blue Pill,
because it sits under the module. The through-hole pins drop into blind
pockets in the floor's inner face, so trim them to 1.5 mm or less under the
PCB. The outer bottom stays closed except for 5 vent slots.

Coordinates: X = KiCad x, Y = BOARD_H - KiCad y (KiCad Y points down), so the
top view matches the PCB editor view. Rectangles below are KiCad footprint
bounding boxes (x0, y0, x1, y1) taken from the .kicad_pcb.
"""

import os
import subprocess

import FreeCAD as App
import MeshPart
import Part

V = App.Vector
OUT = os.path.dirname(os.path.abspath(__file__))

# Board, from the .kicad_pcb
BOARD_W = 77.5
BOARD_H = 67.5
PCB_T = 1.6
HOLES_KICAD = [(73.5, 63.5), (4.0, 63.5), (16.0, 4.5), (63.5, 18.0)]  # MH1..MH4
FAN_HEADERS = [  # J4..J7, Molex 47053-1000
    (8.72, 59.27, 19.87, 66.32), (24.64, 59.27, 35.79, 66.32),
    (41.23, 59.27, 52.38, 66.32), (58.23, 59.27, 69.38, 66.32)]
JST_INPUTS = [  # J10, J11, J8, J9, JST B2B-PH-K
    (11.51, 52.73, 18.46, 58.27), (27.35, 52.73, 34.30, 58.27),
    (43.18, 52.73, 50.13, 58.27), (61.51, 52.73, 68.46, 58.27)]
POWER_IN = (4.0, 0.8, 13.0, 24.8)  # J1 Mate-N-Lok body, measured 9 x 24 mm, centred on its footprint
POWER_IN_H = 12.0                  # J1 top above the PCB underside, measured, the tallest part
USB_Y = 13.84                      # Blue Pill centre line, KiCad y
USB_Z0, USB_Z1 = 5.0, 9.0          # USB receptacle bottom and top edge above the PCB underside, measured
PIN_ROWS = [  # through-hole pad bounding boxes: J1, J2, J3, J4..J7, J10, J11, J8, J9
    (7.25, 3.93, 9.75, 21.67), (25.89, 23.15, 75.85, 24.85), (25.89, 2.83, 75.85, 4.53),
    (9.63, 61.48, 18.98, 63.52), (25.55, 61.48, 34.91, 63.52),
    (42.13, 61.48, 51.48, 63.52), (59.13, 61.48, 68.48, 63.52),
    (13.39, 54.08, 16.59, 55.83), (29.22, 54.08, 32.42, 55.83),
    (45.05, 54.08, 48.25, 55.83), (63.39, 54.08, 66.59, 55.83)]

# Tray
CLEARANCE = 0.5   # gap between PCB edge and wall
WALL = 1.6
FLOOR_T = 1.2
STANDOFF_H = 0.7  # PCB underside height above floor
PIN_SLOT_M = 0.3  # gap around each through-hole pad row in its floor pocket
POCKET_SKIN = 0.4  # closed floor left under each pin pocket; pins must stick out <= 1.5 mm
VENT_C = (38.75, 39.5)  # vent slot group centre, KiCad x, y
VENT_N, VENT_L, VENT_W, VENT_PITCH = 5, 70.0, 2.0, 5.0  # horizontal stadium vent slots
WALL_ABOVE = 2.0  # wall height above PCB top surface
LEDGE_W = 1.0     # edge support ledge under the PCB perimeter
STANDOFF_D = 5.5
SCREW_D = 2.4     # M2 clearance hole
CSK_D = 4.0       # countersink for the DIN 965 M2 head (3.8 mm), 90 degrees
CORNER_R = 2.0

# Cover
TOP_CLEAR = POWER_IN_H - PCB_T  # PCB top to cover underside
TOP_T = 1.1
FIT = 0.15        # gap between cover lip and tray wall, per side
LIP_T = 1.2
LIP_H = 1.8       # stops 0.2 mm above the PCB top
FAN_MARGIN = 1.5  # finger room around each fan header (16.5 mm pitch leaves 2.35 mm ribs)
WIN_MARGIN = 0.5  # gap around the J1 body in its window
MOLEX_CHAMFER = 2.0  # 45 degree chamfer on both left corners of the J1 body; verify against the real part
USB_NOTCH_W = 9.0  # USB hole width (Y), rounded ends, measured
USB_NOTCH_H = 3.0  # USB hole height, measured
FAN_CONN_H = 6.0  # right-angle fan header + cable clearance above the PCB top; verify against the real part

LOGO_TEXT = "Fanny"
LOGO_C = (46.0, 26.0)  # logo centre on the cover top, KiCad x, y, clear of every window
LOGO_BOX = (25.0, 15.0)  # logo fits inside this w x h
LOGO_DEPTH = 0.4  # engraved, so the face-down cover prints without supports
LOGO_FONT = "Liberation Sans:bold"  # fontconfig pattern

EXPLODE = 35.0    # cover lift in the exploded render

PCB_Z = FLOOR_T + STANDOFF_H            # PCB underside
PCB_TOP = PCB_Z + PCB_T
WALL_H = PCB_TOP + WALL_ABOVE           # tray wall top
COVER_Z = PCB_TOP + TOP_CLEAR + TOP_T   # cover top surface
IN_W = BOARD_W + 2 * CLEARANCE
IN_H = BOARD_H + 2 * CLEARANCE
IN_R = max(CORNER_R - WALL, 0.5)

HOLES = [(x, BOARD_H - y) for x, y in HOLES_KICAD]


def rounded_box(x, y, z, w, h, d, r):
    """Box at (x, y, z) of size w x h x d with vertical edges rounded to r."""
    box = Part.makeBox(w, h, d, V(x, y, z))
    vertical = [e for e in box.Edges
                if abs(e.Vertexes[0].Point.z - e.Vertexes[1].Point.z) > 1e-6]
    return box.makeFillet(r, vertical)


def ring(x, y, z, w, h, t, d, r):
    """Closed rectangular ring with outer corner at (x, y, z)."""
    inner = rounded_box(x + t, y + t, z - 1, w - 2 * t, h - 2 * t, d + 2, max(r - t, 0.3))
    return rounded_box(x, y, z, w, h, d, r).cut(inner)


def fuse(shapes):
    return shapes[0].multiFuse(shapes[1:]).removeSplitter()


def fan_openings():
    """One box per fan header, cut into whatever wall material sits at that
    X range: the tray wall, and the cover's lip/band/skirt above it. A right
    angle header's body stays under the wall height, out of the cover's way;
    only the cable and plug need the slot. The box runs past both ends on
    purpose (through the clearance gap and past the inner wall face), since
    cutting where no wall exists is a no-op."""
    z0, z1 = PCB_TOP, PCB_TOP + FAN_CONN_H
    y0, y1 = -CLEARANCE - WALL - 2, 2.0
    boxes = []
    for b in FAN_HEADERS:
        x0 = max(b[0] - FAN_MARGIN, 0.5)
        x1 = min(b[2] + FAN_MARGIN, BOARD_W - 0.5)
        boxes.append(Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0)))
    return boxes


def tray():
    o = -CLEARANCE - WALL
    out_w, out_h = IN_W + 2 * WALL, IN_H + 2 * WALL

    floor = rounded_box(o, o, 0, out_w, out_h, FLOOR_T, CORNER_R)

    walls = ring(o, o, 0, out_w, out_h, WALL, WALL_H, CORNER_R)
    ledge = Part.makeBox(IN_W, IN_H, PCB_Z, V(-CLEARANCE, -CLEARANCE, 0)).cut(
        Part.makeBox(BOARD_W - 2 * LEDGE_W, BOARD_H - 2 * LEDGE_W, PCB_Z + 2,
                     V(LEDGE_W, LEDGE_W, -1)))

    parts = [floor, walls, ledge]
    cuts = []
    for hx, hy in HOLES:
        parts.append(Part.makeCylinder(STANDOFF_D / 2, PCB_Z, V(hx, hy, 0)))
        cuts.append(Part.makeCylinder(SCREW_D / 2, PCB_Z + 2, V(hx, hy, -1)))
        # cone starts 0.5 mm below the floor so its face doesn't sit coplanar with it
        cuts.append(Part.makeCone(CSK_D / 2 + 0.5, SCREW_D / 2, (CSK_D - SCREW_D) / 2 + 0.5, V(hx, hy, -0.5)))
    for x0, y0, x1, y1 in PIN_ROWS:
        m = PIN_SLOT_M
        cuts.append(Part.makeBox(x1 - x0 + 2 * m, y1 - y0 + 2 * m, PCB_Z,
                                 V(x0 - m, BOARD_H - y1 - m, POCKET_SKIN)))
    r = VENT_W / 2
    vx0 = VENT_C[0] - VENT_L / 2 + r
    for i in range(VENT_N):
        vy = BOARD_H - VENT_C[1] + (i - (VENT_N - 1) / 2) * VENT_PITCH
        cuts += [Part.makeCylinder(r, FLOOR_T + 2, V(vx0, vy, -1)),
                 Part.makeCylinder(r, FLOOR_T + 2, V(vx0 + VENT_L - VENT_W, vy, -1)),
                 Part.makeBox(VENT_L - VENT_W, VENT_W, FLOOR_T + 2, V(vx0, vy - r, -1))]
    shape = fuse(parts).cut(Part.makeCompound(cuts))
    return shape.cut(Part.makeCompound(fan_openings() + [usb_notch()]))


def window(b, m):
    """KiCad bounding box grown by m, clipped to keep a rim inside the skirt."""
    x0, x1 = max(b[0] - m, 0.5), min(b[2] + m, BOARD_W - 0.5)
    y0, y1 = max(b[1] - m, 0.5), min(b[3] + m, BOARD_H - 0.5)
    return Part.makeBox(x1 - x0, y1 - y0, TOP_T + 2, V(x0, BOARD_H - y1, COVER_Z - TOP_T - 1))


def molex_window():
    """J1 body outline with its two chamfered left corners, grown by
    WIN_MARGIN and clipped to keep a rim inside the skirt."""
    x0, x1 = POWER_IN[0], POWER_IN[2]
    y0, y1 = BOARD_H - POWER_IN[3], BOARD_H - POWER_IN[1]
    c = MOLEX_CHAMFER
    outline = Part.makePolygon([
        V(x0 + c, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0 + c, y1, 0),
        V(x0, y1 - c, 0), V(x0, y0 + c, 0), V(x0 + c, y0, 0)])
    face = Part.Face(outline).makeOffset2D(WIN_MARGIN, 2)  # join 2: keep the corners sharp
    face.translate(V(0, 0, COVER_Z - TOP_T - 1))
    rim = Part.makeBox(BOARD_W - 1, BOARD_H - 1, COVER_Z + 2, V(0.5, 0.5, -1))
    return face.extrude(V(0, 0, TOP_T + 2)).common(rim)


def logo():
    """LOGO_TEXT scaled to fit LOGO_BOX, centred on LOGO_C, cut LOGO_DEPTH into the cover top."""
    font = subprocess.run(["fc-match", "-f", "%{file}", LOGO_FONT],
                          capture_output=True, text=True, check=True).stdout
    chars = Part.makeWireString(LOGO_TEXT, font, 10.0)
    faces = [Part.makeFace(wires, "Part::FaceMakerBullseye") for wires in chars if wires]
    text = Part.makeCompound(faces)
    bb = text.BoundBox
    k = min(LOGO_BOX[0] / bb.XLength, LOGO_BOX[1] / bb.YLength)
    text.scale(k, V(bb.Center.x, bb.Center.y, 0))
    text.translate(V(LOGO_C[0] - bb.Center.x, BOARD_H - LOGO_C[1] - bb.Center.y, COVER_Z - LOGO_DEPTH))
    return text.extrude(V(0, 0, LOGO_DEPTH + 1))


def usb_notch():
    """USB hole through the right wall, centred on the receptacle: a
    landscape stadium with flat top and bottom and rounded ends. Also cut
    from the tray, in case the hole ever reaches below the wall top."""
    depth = CLEARANCE + WALL + 3
    x0 = BOARD_W - 2
    yc = BOARD_H - USB_Y
    r = USB_NOTCH_H / 2
    z0 = PCB_Z + (USB_Z0 + USB_Z1) / 2 - r
    cap_lo = Part.makeCylinder(r, depth, V(x0, yc - USB_NOTCH_W / 2 + r, z0 + r), V(1, 0, 0))
    cap_hi = Part.makeCylinder(r, depth, V(x0, yc + USB_NOTCH_W / 2 - r, z0 + r), V(1, 0, 0))
    mid = Part.makeBox(depth, USB_NOTCH_W - 2 * r, 2 * r, V(x0, yc - USB_NOTCH_W / 2 + r, z0))
    return fuse([cap_lo, cap_hi, mid])


def cover():
    o = -CLEARANCE - WALL
    out_w, out_h = IN_W + 2 * WALL, IN_H + 2 * WALL
    lip_z = WALL_H - LIP_H

    shell = rounded_box(o, o, WALL_H, out_w, out_h, COVER_Z - WALL_H, CORNER_R).cut(
        rounded_box(-CLEARANCE, -CLEARANCE, WALL_H - 1, IN_W, IN_H, COVER_Z - WALL_H - TOP_T + 1, IN_R))
    # lip that drops inside the tray wall
    lip = ring(-CLEARANCE + FIT, -CLEARANCE + FIT, lip_z,
               IN_W - 2 * FIT, IN_H - 2 * FIT, LIP_T, LIP_H + 2, IN_R)
    # band that joins the lip to the skirt
    band = ring(-CLEARANCE, -CLEARANCE, WALL_H, IN_W, IN_H, FIT + LIP_T, 2, IN_R)

    cuts = [window(b, 0) for b in JST_INPUTS]
    cuts.append(molex_window())
    cuts.append(usb_notch())
    cuts += fan_openings()  # right-angle fan header cable slot, shared with the tray wall
    cuts.append(logo())
    return fuse([shell, lip, band]).cut(Part.makeCompound(cuts))


def export_stl(shape, name):
    mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.02, AngularDeflection=0.2)
    mesh.write(os.path.join(OUT, name))


def build():
    t, c = tray(), cover()
    for name, s in (("tray", t), ("cover", c)):
        if not s.isValid() or len(s.Solids) != 1:
            raise RuntimeError(f"{name}: invalid shape or not a single solid")
        print(f"{name}: volume {s.Volume / 1000:.2f} cm3, about {s.Volume / 1000 * 1.24:.1f} g PLA")

    c_print = c.copy()
    c_print.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    c_print.translate(V(0, BOARD_H, COVER_Z))
    t.exportStep(os.path.join(OUT, "fan_controller_tray.step"))
    c.exportStep(os.path.join(OUT, "fan_controller_cover.step"))
    export_stl(t, "fan_controller_tray.stl")
    export_stl(c_print, "fan_controller_cover.stl")

    doc = App.newDocument("fan_controller_case")
    Part.show(t, "Tray")
    Part.show(c, "Cover")
    Part.show(Part.makeBox(BOARD_W, BOARD_H, PCB_T, V(0, 0, PCB_Z)), "PCB_outline")
    doc.recompute()
    doc.saveAs(os.path.join(OUT, "fan_controller_case.FCStd"))
    return doc


def render(doc):
    import FreeCADGui as Gui

    gdoc = Gui.getDocument(doc.Name)
    gdoc.getObject("Tray").ShapeColor = (0.41, 0.41, 0.41)
    gdoc.getObject("Cover").ShapeColor = (0.27, 0.51, 0.71)
    gdoc.getObject("PCB_outline").ShapeColor = (0.15, 0.45, 0.2)
    gdoc.getObject("PCB_outline").Transparency = 60
    view = gdoc.ActiveView
    for name, lift in (("case_assembled.png", 0.0), ("case_exploded.png", EXPLODE)):
        doc.getObject("Cover").Placement.Base = V(0, 0, lift)
        doc.recompute()
        Gui.updateGui()  # the new view ignores camera calls until it has drawn once
        view.viewIsometric()
        view.fitAll()
        view.saveImage(os.path.join(OUT, name), 1600, 1000, "#F8F8F8")
    doc.getObject("Cover").Placement.Base = V(0, 0, 0)
    Gui.getMainWindow().close()


doc = build()
if App.GuiUp:
    render(doc)
