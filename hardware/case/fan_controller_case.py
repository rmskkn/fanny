"""Two-part printed case for the fan controller PCB (hardware/kicad/fan_controller.kicad_pcb).

tray:  low open tray. The PCB screws onto 4 standoffs. The wall rises only
       2 mm above the PCB, so the tray also works on its own with no cover.
cover: plate with a skirt that sits on the tray wall. A lip goes inside the
       tray wall and holds it by friction. The four fan headers are the
       horizontal, right-angle kind (same pads as the vertical ones the
       .kicad_pcb still shows, different shroud), so their cable leaves
       sideways through a slot cut into the front wall, shared by the tray
       and the cover skirt, instead of a hole in the top. The JST PH sensor
       inputs each get a pyramidal hole through the top plate, wide at the
       outer face and tapering down to the connector's own footprint, to
       guide the small plug in. A plain window sits over the 12 V Mate-N-Lok
       input, and a notch in the skirt leaves the micro-B USB free.

Build (headless): writes fan_controller_case.FCStd, the STEP files and the
print-ready STL files next to this script.
    freecadcmd fan_controller_case.py
Build and render the README images (opens the FreeCAD window briefly):
    freecad fan_controller_case.py

Print: no supports. Tray floor down. The cover STL is flipped, top face down,
so the windows are plain holes and the USB notch opens upward. 0.2 mm
layers, 2 perimeters, 15 % infill. PLA or PETG.
Fasten: 4x M2x6 self-tapping screws (or M2 screws into heat-set inserts with
INSERT_D set to the insert's hole size). Screw MH4 before plugging in the
Blue Pill, because it sits under the module.

Coordinates: X = KiCad x, Y = BOARD_H - KiCad y (KiCad Y points down), so the
top view matches the PCB editor view. Rectangles below are KiCad footprint
bounding boxes (x0, y0, x1, y1) taken from the .kicad_pcb.
"""

import os

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
    (23.02, 52.23, 29.97, 57.78), (39.02, 52.73, 45.97, 58.28),
    (54.02, 52.77, 60.97, 58.32), (70.53, 52.73, 77.48, 58.28)]
POWER_IN = (3.46, 0.91, 13.55, 24.70)  # J1, On-Shore OSTOQ045450 Mate-N-Lok
USB_Y = 13.84                          # Blue Pill centre line, KiCad y

# Tray
CLEARANCE = 0.5   # gap between PCB edge and wall
WALL = 1.6
FLOOR_T = 1.2
STANDOFF_H = 4.0  # PCB underside height above floor; THT leads stick out 1.9 mm
WALL_ABOVE = 2.0  # wall height above PCB top surface
LEDGE_W = 1.0     # edge support ledge under the PCB perimeter
STANDOFF_D = 5.5
INSERT_D = 1.7    # pilot hole for M2 self-tapping screw
CELL = 9.0        # floor lightening cell pitch
RIB = 2.0         # floor rib width
CORNER_R = 2.0

# Cover
TOP_CLEAR = 20.0  # PCB top to cover underside; Blue Pill stack with BOOT jumpers is about 18.6 mm
TOP_T = 1.6
FIT = 0.15        # gap between cover lip and tray wall, per side
LIP_T = 1.2
LIP_H = 1.8       # stops 0.2 mm above the PCB top
FAN_MARGIN = 1.5  # finger room around each fan header (16.5 mm pitch leaves 2.35 mm ribs)
WIN_MARGIN = 1.5  # room around the other connectors
USB_NOTCH_W = 18.0  # notch width (Y), rounded ends; the real USB-C port is 8.34x2.56 mm, too
USB_NOTCH_H = 14.0  # small to pass this micro-B plug (overmould up to 10.6x8.5 mm), so this is
                    # landscape like every real USB connector without going that thin
FAN_CONN_H = 6.0  # right-angle fan header + cable clearance above the PCB top; verify against the real part
NTC_FLARE = 3.0   # extra half-width, outer face only, that flares each NTC funnel's mouth

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

    # floor with a lightening grid inside a solid rim
    cells = []
    lo_x, hi_x = WALL, BOARD_W - WALL
    lo_y, hi_y = WALL, BOARD_H - WALL
    x = 0.0
    while x < BOARD_W:
        y = 0.0
        while y < BOARD_H:
            x0, x1 = max(x + RIB / 2, lo_x), min(x + CELL - RIB / 2, hi_x)
            y0, y1 = max(y + RIB / 2, lo_y), min(y + CELL - RIB / 2, hi_y)
            if x1 - x0 > 0.5 and y1 - y0 > 0.5:
                cells.append(Part.makeBox(x1 - x0, y1 - y0, FLOOR_T + 2, V(x0, y0, -1)))
            y += CELL
        x += CELL
    floor = rounded_box(o, o, 0, out_w, out_h, FLOOR_T, CORNER_R).cut(Part.makeCompound(cells))

    walls = ring(o, o, 0, out_w, out_h, WALL, WALL_H, CORNER_R)
    ledge = Part.makeBox(IN_W, IN_H, PCB_Z, V(-CLEARANCE, -CLEARANCE, 0)).cut(
        Part.makeBox(BOARD_W - 2 * LEDGE_W, BOARD_H - 2 * LEDGE_W, PCB_Z + 2,
                     V(LEDGE_W, LEDGE_W, -1)))

    parts = [floor, walls, ledge]
    pilots = []
    for hx, hy in HOLES:
        parts.append(Part.makeCylinder((STANDOFF_D + 2.4) / 2, FLOOR_T, V(hx, hy, 0)))
        parts.append(Part.makeCone((STANDOFF_D + 2.4) / 2, STANDOFF_D / 2, FLOOR_T + 1.5, V(hx, hy, 0)))
        parts.append(Part.makeCylinder(STANDOFF_D / 2, PCB_Z, V(hx, hy, 0)))
        pilots.append(Part.makeCylinder(INSERT_D / 2, PCB_Z, V(hx, hy, FLOOR_T)))
    shape = fuse(parts).cut(Part.makeCompound(pilots))
    return shape.cut(Part.makeCompound(fan_openings()))


def window(b, m):
    """KiCad bounding box grown by m, clipped to keep a rim inside the skirt."""
    x0, x1 = max(b[0] - m, 0.5), min(b[2] + m, BOARD_W - 0.5)
    y0, y1 = max(b[1] - m, 0.5), min(b[3] + m, BOARD_H - 0.5)
    return Part.makeBox(x1 - x0, y1 - y0, TOP_T + 2, V(x0, BOARD_H - y1, COVER_Z - TOP_T - 1))


def ntc_funnel(b):
    """Pyramidal hole for one JST PH sensor input: snug to the connector's
    own footprint at the inner face, flared by NTC_FLARE at the outer face,
    to guide the small plug through the top plate. The flare shrinks by the
    same amount on every side, so the outer square stays centred on the
    connector instead of clamping each side independently, which could
    otherwise pull one side in past the inner square and invert the taper
    for a header close to the board edge (J9 sits only 0.02 mm shy of it)."""
    x0, x1 = b[0], b[2]
    y0, y1 = BOARD_H - b[3], BOARD_H - b[1]
    flare = max(0.0, min(NTC_FLARE, x0 - 0.5, BOARD_W - 0.5 - x1, y0 - 0.5, BOARD_H - 0.5 - y1))
    small = Part.makePolygon([V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0), V(x0, y0, 0)])
    big = Part.makePolygon([
        V(x0 - flare, y0 - flare, 0), V(x1 + flare, y0 - flare, 0),
        V(x1 + flare, y1 + flare, 0), V(x0 - flare, y1 + flare, 0),
        V(x0 - flare, y0 - flare, 0)])
    small.translate(V(0, 0, COVER_Z - TOP_T - 0.2))
    big.translate(V(0, 0, COVER_Z + 0.5))
    return Part.makeLoft([small, big], True, True)


def usb_notch():
    """Micro-B USB cable notch through the right skirt, open at the bottom:
    a landscape stadium, wide with flat top and bottom and rounded left and
    right ends, like every real USB connector (the real USB-C port is
    8.34x2.56 mm, too small to pass this micro-B plug, so this is sized to
    fit the plug and still read as USB-shaped rather than matching that
    ratio exactly)."""
    depth = CLEARANCE + WALL + 3
    x0 = BOARD_W - 2
    yc = BOARD_H - USB_Y
    z0 = (WALL_H - LIP_H) - 1
    r = USB_NOTCH_H / 2
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

    cuts = [ntc_funnel(b) for b in JST_INPUTS]
    cuts.append(window(POWER_IN, WIN_MARGIN))
    cuts.append(usb_notch())
    cuts += fan_openings()  # right-angle fan header cable slot, shared with the tray wall
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
