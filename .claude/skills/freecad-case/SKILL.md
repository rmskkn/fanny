---
name: freecad-case
description: Edit, rebuild and render the 3D-printed fan controller case (hardware/case/fan_controller_case.py) with FreeCAD. Use whenever the user asks to change the case, tray or cover, move or resize a window, hole, slot, notch or vent, change case height, screws, standoffs or floor, add a logo or text, refit the case after a PCB change (connector moved, part swapped), regenerate the case STL/STEP files or the case_assembled/case_exploded PNGs, or mentions FreeCAD at all, even if they don't say "case".
---

# Fan controller case in FreeCAD

The case is fully parametric Python: `hardware/case/fan_controller_case.py`.
Every size is a constant at the top of that file, and the script writes
`fan_controller_case.FCStd`, both STEP files and both STL files next to
itself. Never edit the `.FCStd` by hand; it is an output.

## Coordinates

Case X = KiCad x. Case Y = `BOARD_H - KiCad y`, so the top view matches the
PCB editor. Rectangle constants are KiCad boxes `(x0, y0, x1, y1)` in mm.
Z = 0 is the tray floor bottom. `PCB_Z` is the PCB underside, `COVER_Z` the
cover top. The cover STL is exported flipped, top face down, so anything on
the top face should be engraved, not raised (raised detail would need
supports).

## When the PCB changed

Read positions from the board, never from memory or old constants:

```bash
python3 .claude/skills/freecad-case/scripts/pcb_boxes.py            # all J/MH refs
python3 .claude/skills/freecad-case/scripts/pcb_boxes.py J8 J9 J10  # chosen refs
```

It prints each footprint's bounding box and, for through-hole parts, the
pad-row bounding box (what `PIN_ROWS` needs). Paste the numbers into the
matching constants.

## Measured values beat footprints

Footprint boxes include silkscreen and courtyard. When the user gives a
caliper measurement (connector body size, height, USB position), use it and
mark the constant "measured" in its comment. State which reference a height
is measured from (PCB underside vs top) when you write it, because mixing
the two shifts the whole case by `PCB_T` (1.6 mm).

## Check the physical stack before cutting height

Total height = `FLOOR_T + STANDOFF_H + (PCB underside to tallest part) + TOP_T`.
Two things limit the floor side: through-hole pins stick 1.9 mm out under
the PCB, and screws need room to grip. If a requested height can't fit both,
stop and ask with concrete options (trim pins, pockets in the floor, nut
from the other side, taller case) instead of silently clipping something.
Also flag the Blue Pill stack against the space above the PCB.

## Live FreeCAD workflow

Keep one FreeCAD GUI open for the whole session and drive it through the
`freecad` MCP server (RPC on 127.0.0.1:9875). Relaunching per change is slow
and loses state.

1. If `mcp__freecad__get_rpc_status` errors, start one instance in the
   background and wait for the port:
   ```bash
   cd hardware/case && (setsid freecad fan_controller_case.FCStd >/dev/null 2>&1 &)
   ```
2. After each edit, rebuild headless. It prints each part's volume and raises
   if a part is not one valid solid, so read that output:
   ```bash
   cd hardware/case && freecadcmd fan_controller_case.py
   ```
3. `mcp__freecad__reload_document` with `doc_name="fan_controller_case"`.
4. A headless save stores every object hidden, so the reloaded view is
   blank. Run the refresh script through `mcp__freecad__execute_code`. It
   restores visibility and colours, rewrites `case_assembled.png` and
   `case_exploded.png`, and leaves the requested view:
   ```python
   VIEW = "Top"  # Isometric, Top, Bottom, Right, Front ...
   exec(open("/home/roman/projects/fanny/.claude/skills/freecad-case/scripts/refresh_view.py").read())
   ```
   Pick the view that shows the change: Top for cover windows and the logo,
   Bottom for the floor (the script hides the PCB there), Right for the USB
   hole. Look at the screenshot before reporting.
5. Optionally check overlaps with the PCB inside the same session:
   `doc.Tray.Shape.common(doc.PCB_outline.Shape).Volume` should be 0.

Never run `freecad fan_controller_case.py` and never `import` the module
inside `execute_code`. Its `render()` ends with
`Gui.getMainWindow().close()`, which quits the shared GUI.

## Before commit

The repo rule applies: `make` and `make test` from the root on the exact
staged tree. Stage the script together with every regenerated output
(`.FCStd`, `*.step`, `*.stl`, `case_*.png`). A rebuild with no geometry
change still rewrites the STEP and FCStd files (timestamps), and that is fine
to commit. `README.md` has a Case section; touch it only when asked, but
tell the user when the change makes it stale (screws, windows, weight).
