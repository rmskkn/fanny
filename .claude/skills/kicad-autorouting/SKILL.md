---
name: kicad-autorouting
description: Route or re-route a KiCad PCB's copper traces manually via Freerouting, bypassing the MCP kicad autoroute tool's fixed timeout. Use this whenever the user asks to autoroute, route, or re-route a board; mentions Freerouting; reports the built-in autoroute tool timing out, hanging, or finishing with 0 tracks/0 vias; or needs the board re-routed after a schematic sync, net merge, or width/netclass change. Also use when checking whether an autoroute result is real or stale (the known swig/IPC divergence bug).
---

# KiCad PCB autorouting via Freerouting

The MCP `autoroute` tool wraps Freerouting with a fixed ~300s timeout and no
way to raise it. On anything but a small board this times out before
Freerouting finishes a single pass, and the failure can leave the board in a
worse state than before (see Gotchas). Running Freerouting directly as a
plain Java process has no such cap and gives visibility into what it's
actually doing — this skill is the reliable way to route, full stop, not just
a fallback.

## Why DSN/SES round-trip and not the one-shot MCP tool

Freerouting is not a KiCad-native router — it imports a Specctra `.dsn`
netlist+outline, routes it in its own engine, and writes a Specctra `.ses`
session back. The MCP `autoroute` tool does this export/run/import for you in
one call bounded by its timeout; this skill does the same three steps as
separate commands you control, so a slow board just takes longer instead of
failing.

This does **not** touch placement, footprints, or net assignments — only
copper. It's the routing equivalent of "update PCB from schematic," not a
substitute for it. If nets are wrong or unassigned, fix that first (sync the
schematic to the board) — Freerouting will happily route a wrong net
perfectly.

## Steps

### 1. Baseline

Before touching anything, know what you're starting from:

```bash
grep -c "(segment" fan_controller.kicad_pcb   # existing track count
grep -c "(via "    fan_controller.kicad_pcb   # existing via count
```

Run ERC/DRC (`mcp__kicad__run_drc`) and note the violation count. If the
board already has 0 tracks, that's expected for a fresh board — otherwise, it
means something upstream (a failed autoroute, a manual edit) already stripped
routing, and that's worth resolving before adding more.

### 2. Export the DSN

```
mcp__kicad__export_dsn(
  boardPath: "<project>/hardware/kicad/<board>.kicad_pcb",
  outputPath: "<project>/hardware/kicad/<board>.dsn"
)
```

This bakes the current netclasses (track width, clearance) into the DSN, so
Freerouting routes at the widths already configured in KiCad — check the
tool's `netClasses.applied` field in the result to confirm it picked up what
you expect (e.g. a `Power` class for wide nets).

### 3. Run Freerouting directly

Locate the jar once per machine — this project found it at
`~/.kicad-mcp/freerouting.jar` (installed by the kicad MCP server itself).
Confirm with `mcp__kicad__check_freerouting` if unsure, or:

```bash
find ~ -maxdepth 3 -iname "freerouting*.jar" 2>/dev/null
java -version   # Freerouting 2.5.0 wants Java 17+; this project has 26
```

Then run it with no timeout wrapper:

```bash
cd <project>/hardware/kicad
java -jar ~/.kicad-mcp/freerouting.jar \
  -de <board>.dsn \
  -do <board>.ses \
  -mp 500 \
  -mt $(( $(nproc) - 1 ))
```

- `-de` input DSN, `-do` output session (`.ses`, not `.dsn` — the importer
  expects Specctra session format)
- `-mp` max passes — raise this for a denser board; Freerouting stops early
  once it can't improve further, so a high number costs time, not quality
- `-mt` thread pool for route optimization — one less than core count is
  Freerouting's own default; only override it if cores are contended

Let it run in the foreground or background per the user's preference — it
can legitimately take several minutes on a busy board. There's no silent
failure mode here: it prints progress and either writes the `.ses` or exits
with a clear error (timeout is now yours to set by patience, not by this
tool's hard cap).

### 4. Import the result

```
mcp__kicad__import_ses(
  sesPath: "<project>/hardware/kicad/<board>.ses"
)
```

### 5. Verify — never trust a clean exit alone

```
mcp__kicad__run_drc()
```

And re-check the raw file, the same way you took the baseline:

```bash
grep -c "(segment" fan_controller.kicad_pcb
grep -c "(via "    fan_controller.kicad_pcb
```

Track/via counts should have gone up (or Freerouting would have reported
"already routed" / near-zero ratsnest). If DRC shows new clearance or
width violations, that's almost always a netclass that needs adjusting
before the next routing pass, not a Freerouting bug.

## Gotchas

- **MCP `autoroute`'s 300s timeout can corrupt instead of just failing.**
  On a board this project tested, a timed-out `autoroute` call left the
  on-disk `.kicad_pcb` with 0 segments and 0 vias — worse than before the
  call, not merely "no progress." If you've already hit this, re-check the
  baseline step above before routing again; you may be routing from a
  stripped board, not the one you think you have.
- **IPC vs. file (swig) backend divergence.** KiCad's MCP server can have a
  live IPC connection to an open GUI *and* a separate file-based (swig)
  write path. A write from one doesn't always show up when you query via the
  other. After import, if `get_board_info` or the GUI claims 0 tracks right
  after a routing pass that should have added hundreds, check the on-disk
  file directly with `grep` before concluding the routing was lost — the GUI
  may just be reading stale state. Conversely, never let a stale-reading GUI
  save the board — that would overwrite the newly-routed file with its own
  stale view.
- **Do not hand-route individual traces as a workaround for a slow or
  partial autoroute.** If some nets fail to route, that's a sign of a design
  problem (impossible clearance, missing netclass, wrong net assignment) —
  fix the cause and re-run Freerouting on the whole board rather than
  patching with `route_pad_to_pad` calls, which don't participate in the
  same optimization pass and can leave crossing/clearance errors Freerouting
  itself would have avoided.
- **Always diff or re-check DRC after import** — a session file can route
  cleanly in Freerouting's own view and still introduce a KiCad-side DRC
  violation (e.g. silkscreen overlap, solder mask bridge) that Freerouting
  doesn't model.
