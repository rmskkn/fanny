---
name: kicad-pcb-render
description: Render the PCB's 3D view to a PNG via kicad-cli, no GUI needed. Use when asked to render, re-render, update, or regenerate a 3D PCB image (e.g. hardware/pcb_3d.png), or when a component shows as a flat/blank block in the 3D viewer.
---

# Render PCB 3D view via kicad-cli

No MCP tool renders a PCB to a raster 3D image directly: `export_3d`/`export_3d_cli`
only write STEP/STL/GLB/etc geometry files, and `get_board_2d_view` is a flat
top-down layer plot, not a 3D render. `kicad-cli pcb render` does this straight
from the `.kicad_pcb` file with no GUI, no running KiCad instance, no
atspi/X11 screenshot dance.

## Command

```bash
kicad-cli pcb render \
  --output hardware/pcb_3d.png \
  --width 1600 --height 900 \
  --quality high \
  --floor \
  --rotate "-45,0,45" \
  --background opaque \
  hardware/kicad/fan_controller.kicad_pcb
```

- `--rotate "-45,0,45"` gives an isometric-ish view; adjust per board if a
  different angle reads better.
- `--quality high` and `--floor` (shadows/post-processing) cost render time
  (tens of seconds) but match what the GUI 3D viewer would show at its
  default "high" preset — worth it for a committed reference image.
- `--background opaque` avoids a transparent PNG that renders oddly when
  embedded in `README.md` on a light or dark background.
- Takes 10-20s to render on a moderately complex board. Safe to run
  synchronously.

## After rendering

Read the PNG back and look at it. A part that should show a 3D body but
renders as a flat/blank block means its footprint's `model` property points
at a file that doesn't exist on this machine — see Gotchas.

## Gotchas

- Missing-3D-model root cause: footprints often point `model` at
  `${KICAD10_3DMODEL_DIR}/<Lib>.3dshapes/<name>.step`, KiCad's system
  library. If that file isn't bundled with the installed KiCad version
  (check `/usr/share/kicad/3dmodels/<Lib>.3dshapes/`), the part renders
  blank with no error — `kicad-cli` fails silently on this the same way the
  GUI viewer does. If the project vendors its own copy under
  `hardware/kicad/3dmodels/<Lib>.3dshapes/`, repoint the footprint's `model`
  field at `${KIPRJMOD}/3dmodels/<Lib>.3dshapes/<name>.step` instead — grep
  the `.kicad_pcb` for the broken path string and fix every instance with
  one `replace_all` edit (it's one model per footprint type, not per
  instance).
- `kicad-cli` reads the board file straight off disk. If KiCad is also open
  on it with unsaved changes, the render reflects the saved file, not the
  open editor's in-memory state — save in the GUI first, or just trust the
  file if you made edits via MCP tools (those write straight to disk).
- Debug log lines (`Adding duplicate image handler...`, `Build Tech layer
  N`, `Rendering: NN %`) print to stdout/stderr on every run — not errors,
  just progress noise. "Successfully created 3D render image" at the end is
  the success signal.

## Output convention

This project keeps the render at `hardware/pcb_3d.png`, referenced from
`README.md`. Regenerate it after any placement change worth reflecting
there.
