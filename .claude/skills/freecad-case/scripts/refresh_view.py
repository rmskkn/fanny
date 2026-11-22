# Run inside the live FreeCAD GUI via execute_code, after reload_document:
#   VIEW = "Top"; exec(open(".../refresh_view.py").read())
# Restores visibility and colours, rewrites the README renders, leaves VIEW on screen.
import os

import FreeCAD as App
import FreeCADGui as Gui

doc = App.getDocument("fan_controller_case")
gdoc = Gui.getDocument(doc.Name)
colours = {"Tray": (0.41, 0.41, 0.41), "Cover": (0.27, 0.51, 0.71), "PCB_outline": (0.15, 0.45, 0.2)}
for name, colour in colours.items():
    gdoc.getObject(name).Visibility = True
    gdoc.getObject(name).ShapeColor = colour
gdoc.getObject("PCB_outline").Transparency = 60

view = gdoc.ActiveView
out = os.path.dirname(doc.FileName)
for png, lift in (("case_assembled.png", 0.0), ("case_exploded.png", 35.0)):
    doc.getObject("Cover").Placement.Base = App.Vector(0, 0, lift)
    doc.recompute()
    Gui.updateGui()
    view.viewIsometric()
    view.fitAll()
    view.saveImage(os.path.join(out, png), 1600, 1000, "#F8F8F8")
doc.getObject("Cover").Placement.Base = App.Vector(0, 0, 0)
doc.recompute()

VIEW = globals().get("VIEW", "Isometric")
gdoc.getObject("PCB_outline").Visibility = VIEW != "Bottom"
getattr(view, "view" + VIEW)()
view.fitAll()
