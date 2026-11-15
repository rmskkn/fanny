---
name: kicad-strip-zone
description: Delete a copper zone (GND/power pour, or any net's fill zone) from a KiCad .kicad_pcb file when the kicad MCP server can't do it directly — its delete_graphic tool only handles graphic/drawing items, not zones, and there is no dedicated delete_zone tool. Use this whenever the user asks to remove a GND plane, power plane, or copper pour from a KiCad board, says "route GND as traces instead of a plane," or any time query_zones finds a zone that needs deleting and delete_graphic reports "Graphic not found" for its UUID. Also covers the follow-up: after a zone is stripped, DRC will report unconnected_items for the pads it used to carry (zones usually carry real connectivity, not just extra copper), so routing must be redone afterward with real traces or an autorouter.
---

# Strip a KiCad copper zone by net

## When this applies

A KiCad PCB zone (copper pour / plane) is a distinct item type from graphics,
tracks, or vias. The kicad MCP server exposes `query_zones` to find zones and
`add_zone` to create them, but no tool to delete one — `delete_graphic` only
matches graphic/drawing UUIDs and returns "Graphic not found" if you pass it
a zone's UUID. When a zone needs to go (e.g. the user wants GND routed as
discrete traces instead of a plane, or a stale/incorrect pour needs
removing), editing the `.kicad_pcb` file directly is the way to do it.

Zone blocks can be large — a filled zone's `(filled_polygon ...)` section
lists every point of every fill island, often thousands of coordinates. Don't
`Read` the file to find and edit this by hand; it burns context for no
reason and risks a bad edit on a block you can't fully see. `scripts/strip_kicad_zone.py`
finds each top-level `(zone ...)` block by tracking paren depth, so it never
needs to load the polygon data to know where a block starts and ends.

## Workflow

1. **Close the MCP board session first**, saving nothing:
   `mcp__kicad__close_project(save: false)`. The MCP server holds its own
   in-memory copy of the board; if you edit the file on disk while it still
   thinks it owns that state, a later MCP write can clobber your edit (or an
   MCP read can just show stale data).
2. **Run the script** against the `.kicad_pcb` file, naming the net(s) whose
   zone(s) should go:
   ```bash
   python3 scripts/strip_kicad_zone.py <path/to/board.kicad_pcb> --net GND
   ```
   This defaults to a dry run — it lists the matching zone block(s) by line
   range and net, and changes nothing. Confirm the match looks right (right
   net, right count — a board can have one zone per copper layer for the
   same net) before applying.
3. **Apply it**: add `--apply`. The script copies the original file to
   `<board>.kicad_pcb.bak` before writing, then removes the matched blocks.
4. **Reopen the board**: `mcp__kicad__open_board(boardPath: ...)`. Confirm
   the zone is really gone with `mcp__kicad__query_zones(net: "<net>")` —
   it should report `zoneCount: 0` for that net.
5. **Run DRC and expect `unconnected_items`.** A zone usually isn't just
   decorative extra copper — pads inside it often connect to it with a short
   thermal-relief stub rather than a direct trace to every other pad on the
   net. Removing the zone can turn "fully connected net" into "every pad on
   that net is now unconnected." Check `mcp__kicad__run_drc()`: if
   `unconnected_items` jumped by roughly the pad count of that net, the zone
   was carrying real connectivity, and the net needs to be routed with
   traces (hand-routed, or via Freerouting — see the `kicad-autorouting`
   skill if one is available in this project) before the board is usable
   again. Don't treat "zone deleted, DRC still clean" as the finish line
   without checking this.

## Multiple nets or multiple zones per net

`--net` can be repeated to strip several nets in one pass:
```bash
python3 scripts/strip_kicad_zone.py board.kicad_pcb --net GND --net +12V --apply
```
A single `--apply` run handles however many matching blocks there are (e.g.
one zone on `F.Cu` and one on `B.Cu` for the same net) — the script reports
every match it found before writing.
