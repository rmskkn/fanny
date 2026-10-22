# Session Summary — KiCad fan controller hardware work

Date: 2026-10-21
Repo: `hardware/kicad/fan_controller.{kicad_pcb,kicad_sch,kicad_pro}`

## Tools used and results

### Git / shell
- `Bash` (`git status`, `git diff`, `git add`, `git commit`, `git log`) — staged and committed 4 commits this session:
  - `8ed5d90` — resave project in KiCad 10 file format (pure reformat, no circuit change)
  - `3d7ac59` — add `.gitignore` entries for KiCad lock files, `.history/`, DRC export
  - `6827fa3` / `c66fd72` — route 21 originally-unrouted nets, remove dead-end via/track stubs, remove TP2/TP3 test points
  - `7dbc83a` — swap J1 connector, drop main-board USB, rotate J3, relocate power-input cluster
- `kicad-cli pcb drc --severity-all --exit-code-violations` (run ~25 times via Bash) — the authoritative connectivity/clearance check used throughout. Discovered the MCP backend's own `run_drc`/`get_drc_violations` tools **do not** run the `unconnected_items` check, which was the root cause of several false "0 errors" readings early in the session.

### KiCad MCP tools (`mcp__kicad__*`)
- `open_board` / `open_project` / `save_board` — board session lifecycle, used continuously between edits.
- `get_ratsnest` — found to be a raw pad-distance MST estimate, **not** a real unrouted-connection indicator (always nonzero even on fully-routed nets); abandoned in favor of native DRC.
- `run_drc` / `get_drc_violations` — used early, found unreliable (see above); replaced by native `kicad-cli` for all later checks.
- `get_component_pads`, `get_component_properties`, `get_net_pads`, `query_traces`, `get_nets_list`, `get_board_extents`, `get_board_2d_view` — inspection tools used extensively to locate pads, trace geometry, and render board snapshots for visual verification.
- `delete_trace` (by UUID and by net) — removed dangling via/track stubs (20 vias + 9 stubs in initial cleanup), orphaned USB_DP/USB_DM traces (59), orphaned FAN*_TACH/FAN*_PWM traces across multiple reroute cycles (150+), and various short-causing traces introduced mid-session.
- `add_via`, `route_trace`, `route_pad_to_pad`, `modify_trace` — manual routing/repair for specific net gaps and short fixes.
- `autoroute` (Freerouting via Docker) — called ~6 times, best-of-3 attempts each, to bulk-route: the original 21 missing nets, J1's new pads, J3's nets after each reposition, and the relocated power cluster. Typical run: 25–300s, 0–3 nets occasionally left for manual finishing (GND and +3V3A were consistently the hardest for the router to close automatically).
- `check_freerouting` — confirmed Docker-based Freerouting 2.5.0 available.
- `delete_component`, `place_component`, `move_component`, `rotate_component` — removed J4/U1/R3 (USB path); placed/repositioned J1 (new connector) and relocated F1/D1/C1/C2/FB1 (power-input cluster) to resolve a courtyard collision with J3.
- `create_footprint`, `register_footprint_library` — built a custom `ConnHeader_MateNLok_1x04_P5.08mm_Vertical` footprint for J1 from the real TE Connectivity 350424-1 datasheet (pitch, drill size), registered as a project-local `fanctl.pretty` library (no footprint libraries exist in this environment otherwise).
- `get_schematic_component`, `get_schematic_pin_locations`, `delete_schematic_component`, `batch_add_and_connect`, `set_schematic_component_property`, `run_erc` — schematic-side edits mirroring the PCB changes (J1 symbol swap, J4/U1/R3/TP2/TP3 removal), kept in sync throughout.
- `check_placement_clearance`, `suggest_placement` — validated the relocated power-cluster placement; `suggest_placement`'s auto-layout was tried once but produced more overlaps than the manual placement, so manual placement was used instead.
- `search_parts_registry`, `digikey_search_parts` — part lookup; DigiKey required unset API credentials (not configured in this environment), parts registry had no exact match for the TE part.

### Web / research tools
- `WebSearch` — found distributor listings for TE Connectivity part 350424-1.
- `ctx_fetch_and_index` (context-mode plugin) — fetched distributor pages and the TE datasheet PDF without pulling raw bytes into the conversation.
- `Read` (with `pages` param) — read the TE 350424-1 datasheet PDF visually (it's a scanned image PDF, no extractable text) to get the real mechanical drawing: 0.200" (5.08mm) pitch, 0.070" (1.78mm) drill, shrouded 4-circuit header, 12A/250V.

### File edit tools
- `Read` / `Edit` / `Write` — direct `.kicad_pcb`/`.kicad_sch` s-expression edits where MCP tools couldn't reach (embedding the new J1 footprint block by hand since the MCP backend's footprint-library cache doesn't live-reload; fixing the `fp-lib-table` to use `${KIPRJMOD}` instead of an absolute path before commit).

### Coordination tools
- `AskUserQuestion` — used 4 times for decisions with real tradeoffs: dangling-via cleanup scope, +3V3 test-point removal scope, connector-fit resolution strategy (revert J3 vs. redesign), and footprint-data sourcing strategy.

## Key findings
1. The MCP KiCad backend's DRC wrapper silently omits the `unconnected_items` check — always verify with native `kicad-cli pcb drc --severity-all`.
2. `get_ratsnest` is not a connectivity check; it's a theoretical pad-distance estimate.
3. Rotating/moving a multi-pin footprint rotates around its **pin-1 origin**, not its geometric center — a naive 180° rotation can shoot a whole pin row off the board edge.
4. This board has zero footprint libraries registered in the current environment; footprints are embedded directly in the `.kicad_pcb` file and referenced by name only for cosmetic/editor purposes.
5. The physically larger replacement connector (TE 350424-1, 23.24mm) does not fit in J1's original corner without displacing neighboring parts — confirmed by exhaustive free-space search before redesigning.

## Known incomplete state (as of last commit `7dbc83a`)
Native DRC still reports 5 unconnected items, 7 shorts, and 3 track crossings, localized to the relocated F1/D1/C1/C2/FB1 cluster and nearby NRST routing. `hardware/tools/gen_kicad.py` (the board's generator / BOM source of truth) has not been updated to match today's schematic/PCB changes.

## 2026-10-22 — `hardware/pcb_3d.png` render command

Via `kicad-cli pcb render` directly (no MCP tool exposes this; `mcp__kicad__export_3d`/`export_3d_cli` only produce STEP/GLB/STL/etc., not a raytraced PNG):

```
kicad-cli pcb render --side top --rotate "-20,0,350" --zoom 0.75 --perspective --floor \
  --background transparent --quality high --width 480 --height 500 \
  -o hardware/pcb_3d.png hardware/kicad/fan_controller.kicad_pcb
```

Notes:
- `--rotate X,Y,Z`: X tilts up/down, Z spins left/right around the vertical axis.
- `--zoom 0.75` (< 1 = zoomed out): needed to stop perspective + rotation from clipping the board edges/mounting holes at the default `--zoom 1`.
- `--background transparent` only applies to the PNG/JPEG raytrace renderer; there is no SVG output path for the 3D view (`export_pcb_svg`/`export_svg` only do flat 2D layer plots, not the raytraced 3D model).
- `--quality high` is the highest fixed tier (`basic`/`high`/`user`/`job_settings`); rendering straight at 480×500 instead of supersampling-and-downsampling to keep the command and output size simple.
- Output: 480×500px, RGBA with a real alpha channel (verified via `PIL`), ~176KB.

## 2026-10-22 — DRC silk violations: full history and corrections

Starting point, once the dangling tracks/vias (10 items), the J1 footprint-library mismatch, and the missing-footprint-library warnings were already cleared: **66 warnings, 0 errors** — `silk_overlap: 55`, `silk_over_copper: 11`.

(That 66 itself is higher than the original 50+11=61 seen at session start: resyncing 68 footprints to the real, now-installed KiCad libraries — see `242605a` — replaced undersized legacy silk/courtyard graphics with correct ones, which *exposed* 5 more genuine clearance collisions that had been hidden by the old undersized graphics.)

### Root cause
Default reference-designator silkscreen offset (e.g. −1.43mm for 0603, −2.4mm for SOT-23) lands on copper or a neighbor's silk because components are placed at ~3.6mm pitch in repeating per-channel rows (FAN1–4: F/Q/R/C groups; AIN1–6: R/C/diode breakout row).

### Pass 1 — commit `ee91801`: 66 → 19
Hid (`F.SilkS` `visible=false`, via `move_footprint_text`) the reference-designator text on 21 footprints, chosen because the violation coordinate matched that footprint's own text position exactly or within ~1mm:
`C2, C10, F1, Q1, R5, R6, R7, R9, R10, R11, R13, R14, R17, R18, R19, R22, R23, R26, R29, R31, TP4`

Result: `silk_over_copper` fully cleared (11 → 0); `silk_overlap` 55 → 19.

### Pass 2 — commit `d02c561`: 19 → 14
Image-based inspection (`get_board_2d_view`, F.SilkS-only layer) found more text-vs-silk collisions in the AIN breakout row and FAN2 channel not caught by the coordinate-matching heuristic above. Hid 6 more:
`D2, D3, D4, R22` (re-hidden, harmless), `R25, Q2`

Result: 19 → 14, all remaining `silk_overlap`.

### Failed attempt (reverted)
Tried stripping D2/D3/D4's own `F.SilkS` body-outline graphics (`fp_line`) entirely, on the theory the diode bodies were the colliding geometry. Made it **worse**: 19 → 22 (`silk_overlap` stayed at 19 unchanged, `lib_footprint_mismatch` appeared for 3 instances since deleted graphics no longer matched the library). Reverted via `git checkout`.

### Remaining 14 `silk_overlap` (open, not yet fixed)
Genuine body-outline-vs-body-outline collisions between two *correctly* library-matched footprints — not reference text, so hiding text can't fix these. Confirmed via silk-only 2D render + coordinate/geometry search:

| Pair | Components | Location |
|---|---|---|
| PinHeader_1x03 vs BAT54S pin-1 mark | `J10` ↔ `D5` | AIN1-6 breakout row |
| PinHeader_1x03 vs BAT54S pin-1 mark | `J11` ↔ `D6` | AIN1-6 breakout row |
| PinHeader_1x03 vs BAT54S pin-1 mark | `J12` ↔ `D7` | AIN1-6 breakout row |
| PinHeader_1x03 vs SOT-23 pin-1 mark | `J13` ↔ `Q3` | AIN1-6 breakout row |
| PinHeader_1x03 vs SOT-23 pin-1 mark | `J14` ↔ `Q4` | AIN1-6 breakout row |
| SOT-23 body silk vs 0603 body silk | `Q2` ↔ `R11` | FAN2 channel |

Why these are stuck: trimming either side's silk graphics to clear the overlap makes that footprint deviate from its real KiCad library copy, which reopens `lib_footprint_mismatch` (demonstrated by the reverted attempt above) — a straight trade, not a fix. The only real fix is nudging one component in each pair by roughly 0.3–1mm, which risks disturbing already-routed traces and may be constrained by external harness/connector pitch on J9–J14. Left open pending a placement-nudge decision.

### Final DRC state (as of commit `d02c561`)
0 errors, 14 warnings, all `silk_overlap`, all in the table above.
