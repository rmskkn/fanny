---
name: pcb_route
description: Fix PCB DRC errors (shorts, solder-mask bridges, hole clearance) by rip-up/reroute of just the offending nets. Use KiCadRoutingTools first; fall back to kicad MCP IPC calls (route_trace/delete_trace) only for a single isolated segment. Use whenever DRC reports errors tied to specific nets, or after a schematic sync leaves bad copper on a few nets.
---

# PCB DRC fix: rip-up/reroute offending nets

Goal: clear DRC **errors** (not cosmetic warnings) by rerouting only the nets
that actually violate, not the whole board. Narrowest fix that passes
`kicad-cli` DRC, verified independent of the flaky MCP backends.

## Order of attempts

1. **KiCadRoutingTools, scoped to the offending net(s)** — primary method,
   below.
2. **kicad MCP IPC calls** (`delete_trace` + `route_trace`) — fallback, only
   for a single isolated bad segment (one short jog around one hole/pad).
   Never use this to re-lay a whole net end-to-end; it has no pathfinding and
   the IPC backend has a documented stale-state bug (see Gotchas).

## Step 1: Map violations to nets

```
mcp__kicad__get_drc_violations(severity: "error")
```

Read `items[].description` for the net name on each side of every
`solder_mask_bridge` / `shorting_items` / `hole_clearance` / `clearance`
error. Group by net. Almost always 1-3 nets account for every error — that's
the rip-up scope, not the whole board.

## Step 2: KiCadRoutingTools setup

```bash
export PYTHONPATH="$HOME/.venvs/kicad-deps/lib/python3.14/site-packages"
cd ~/projects/KiCadRoutingTools
```

Get the board's real design rules first — do not guess a clearance value:

```
mcp__kicad__get_design_rules()
```

Use its `clearance`, `trackWidth`, `holeClearance` values as the `--clearance`
/ `--track-width` / `--hole-to-hole-clearance` flags below. **Do not round up
to a "nicer" number like 0.3mm "for safety"** — on a board already laid out
near its real floor, a wider clearance than the board actually uses can make
the router's own improvement gate reject the run (a pad goes unconnected) and
auto-revert, even though it looks like harmless caution. Confirmed on this
project: `--clearance 0.3` on a full GND reroute broke one pad and was
auto-reverted by KRT; `--clearance 0.127` (the board's actual Default
netclass value) routed the same net clean.

## Step 3: Route — write to a scratch file first, never the live board

```bash
python3 py_router/route.py \
  <project>/hardware/kicad/<board>.kicad_pcb \
  <scratch>/<board>_fix.kicad_pcb \
  --nets NET1 NET2 \
  --rip-existing-nets NET1 NET2 --force-reroute \
  --clearance <from get_design_rules> \
  --track-width <from get_design_rules> \
  --hole-to-hole-clearance <from get_design_rules> \
  --stats
```

- `--nets` + matching `--rip-existing-nets` + `--force-reroute`: rips exactly
  those nets and re-lays them, nothing else.
- For more than 1-2 small nets, run with `--preview` first and read the
  `would ADD` / `would REMOVE` segment/via counts before writing anything —
  that's the real blast radius, not a guess.
- Read `JSON_IMPROVEMENT_GATE` in the output: `"verdict": "accept"` means no
  net was disconnected or worsened. `"reject"` means the run reverted itself
  — do not re-run with looser settings hoping it clears; change the clearance
  to the board's real value instead (see Step 2) or narrow the scope further.
- Expect **collateral reroutes**: KRT may rip a *different* net that was
  physically blocking the one you asked for (reported as `preexisting_rips`).
  That's normal and fine as long as the gate accepts — but check
  `design_rules.narrowed` in the JSON for any net squeezed thinner than the
  board's own default at a "terminal escalation." It will still be inside the
  board's absolute minimums (also from `get_design_rules`: `minTrackWidth`,
  `minViaDiameter`) or the gate would have rejected it — but note it, since
  it's a deliberate departure from the board's normal width in one tight
  spot, not a defect.
- `--rip-existing-nets '*'` plus a huge net scope (e.g. the whole `GND` net
  on a board with no copper pour) is a legitimate thing to run when a single
  bad segment on that net can't be fixed in isolation (see Step 4) — but
  it's a real full-net reroute, not a local patch. Preview it first.

## Step 4: One isolated bad segment — hand-patch instead

If exactly one short segment is the problem (e.g. one track 0.1mm short of a
mounting-hole's clearance) and a full-net KRT reroute is overkill or keeps
colliding with a neighboring net in a tight pocket, edit the `.kicad_pcb`
text directly on the **scratch copy**, not the live board:

1. `grep -n "<uuid-or-coordinates>" <scratch-file>` to find the exact
   `(segment ...)` block.
2. Replace it with a short detour (2-3 segments) that clears the obstacle by
   at least `hole_radius + track_halfwidth + required_clearance` from the
   hole/pad center — compute this, don't eyeball it.
3. **Check what else occupies that detour's path** before committing — query
   `mcp__kicad__query_traces` with a `boundingBox` around the new path on
   both copper layers. A detour that clears the original obstacle can clip a
   third net running right next to it (happened on this project: dodging a
   mounting hole put the new segment 0.14mm from an unrelated `FAN4_RB`
   track). If the pocket is too tight on both layers, that's a real routing
   conflict — stop and ask rather than force a narrower-than-floor trace.
4. Give new segments fresh UUIDs (`python3 -c "import uuid; print(uuid.uuid4())"`),
   don't reuse the deleted one.

## Dangling copper (warnings, not errors)

Always remove `via_dangling` / `track_dangling` DRC warnings — don't leave
them as accepted, unless checking first shows the item is load-bearing (see
below). Treat this the same way regardless of whether the user names it
explicitly — standing rule for this project, not a one-off.

**Do not assume a dangling item is a dead stub.** `via_dangling` /
`track_dangling` means an item has an end DRC can't see a neighbor at — it
does NOT mean the item is unused. Confirmed on this project: deleting 2
dangling vias + 2 dangling track segments that looked like pure leftover
stubs instead broke 1 real net connection and turned 3 *other*, previously
fine segments dangling too (they had been relying on the deleted item as
their own connection point). Before deleting any dangling item:

1. Delete it on a scratch copy of the file, never the live board directly.
2. Re-run `kicad-cli pcb drc` on that copy.
3. Compare `Found N unconnected items` and the full violation list against
   the baseline. If unconnected items increased, or new `*_dangling` entries
   appeared elsewhere, the deleted item was load-bearing — revert it and
   leave the warning as a known, accepted one rather than hand-waving a fix.
4. Only copy the scratch file over the live board once the comparison shows
   a strict improvement (fewer or equal warnings, same 0 unconnected items).

Practical steps:

- Get exact UUID + position from
  `mcp__kicad__get_drc_violations(severity: "warning")`.
- No dedicated via-delete MCP tool exists; `mcp__kicad__delete_trace` with
  `traceUuid` works for both tracks and vias (KiCad treats a via as a track
  subtype internally).
- Prefer the direct-file-edit + `kicad-cli` path from Step 4/5 over IPC
  calls for this — same desync risk, and removing a handful of dangling
  items is exactly the kind of isolated edit that path is for.

## Avoid small/short track pieces

When routing or patching (KRT or hand-edit), prefer fewer, longer segments
over many short ones. Short chopped-up pieces are more likely to leave a
stray dead-end stub behind after a rip-up/reroute or a hand patch (see
above), and they clutter the board for no electrical benefit. If a hand
patch needs a detour, use the minimum number of segments that clears the
obstacle (usually 2-3 for a jog around a hole/pad), not a finely subdivided
path.

## Step 5: Verify — kicad-cli only, not the MCP backends

```bash
kicad-cli pcb drc --refill-zones --format json -o <scratch>/drc.json <scratch-file>
```

This is the ground truth. **Do not trust `mcp__kicad__run_drc` /
`get_drc_violations` alone to confirm a fix** — they share the same
IPC-vs-file desync risk described below, and `kicad-cli` reads straight off
disk with no live session in the loop.

Only after `kicad-cli` shows 0 errors on the scratch file:

```bash
cp <scratch-file> <project>/hardware/kicad/<board>.kicad_pcb
```

Then re-run `kicad-cli pcb drc` on the **installed** path too — confirm the
copy landed correctly, don't assume `cp` succeeded.

## Gotchas

- **IPC backend can silently serve a stale in-memory board.** The kicad MCP
  server has two backends: `ipc` (a live, realtime-connected KiCad GUI
  session) and `swig` (file-based, used by `run_drc`/`get_drc_violations`).
  `reload_board()` on the IPC side can report `"success": true` without
  actually re-reading the file from disk. If you then make IPC-side edits
  (`delete_trace`, `route_trace`) and `save_board()`, it writes out the
  **stale** pre-reload state — silently reverting any fix you'd just
  installed by copying a file onto disk. This happened on this project: a
  `cp` of a clean, KRT-fixed board was immediately undone by a `save_board()`
  call a few tool calls later, because the intervening `reload_board()` never
  actually picked it up.
  - **Never chain `cp` (external file write) → `reload_board` → IPC edits →
    `save_board`** and trust the result. Either do everything through IPC
    calls, or everything through direct file edits + `kicad-cli` — don't mix
    within one fix.
  - After installing a fix via direct file copy, call `reload_board()` as a
    best effort for the live GUI, but verify with `kicad-cli`, not
    `get_board_info`/`run_drc`, and tell the user to **File → Revert** in the
    KiCad GUI if they have the board open — the MCP call cannot force that.
- **`grep -c "(via "` and similar track/via counts can read 0 even when vias
  exist** — the S-expression token is followed directly by a newline with no
  trailing space in some file versions. Don't use a 0 count alone as proof of
  "no vias"; cross-check against `mcp__kicad__get_board_info`'s `viaCount`.
- **`mcp__kicad__add_mounting_hole` deterministically crashes the IPC path.**
  It hits `FromProtoEnum<LINE_STYLE, StrokeLineStyle>` assert in KiCad's own
  API server every time, not an occasional fluke — confirmed by repeating
  the exact same call and getting the exact same assert each time. The tool
  falls back to the file-based (swig) backend and writes correctly anyway,
  so this is safe to ignore; just expect the first attempt to report
  `"Timed out"` and don't bother retrying over IPC, go straight to re-calling
  the same tool (it'll use swig).
- **The swig-backend write can silently no-op if disk changed since the MCP
  session last loaded it.** It returns `"success": true` with the mutation
  applied only in its own memory, and a separate `warnings` /
  `autoSave: {"saved": false, "diskChangedExternally": true}` field — easy to
  miss if you only check the top-level `success`. This happens whenever a
  file was written outside the MCP (e.g. a `cp` from KRT output, as in Step
  5). Fix: `mcp__kicad__open_project` (or `reload_board`) to resync the
  session to disk, then re-issue the write. Always check for this field on
  any MCP write after installing a file via direct copy.
- **A connectivity checker is not a DRC checker.** KiCadRoutingTools'
  `check_connected.py` only flags unrouted/broken nets — two different nets
  shorted together look "fully connected" to it, since electrically they are.
  Only `check_drc.py` or `kicad-cli pcb drc` catches a short, and
  `check_drc.py` itself does not model solder-mask bridges or hole clearance
  at all (its own README says so) — use `kicad-cli` for anything involving
  those violation types.
