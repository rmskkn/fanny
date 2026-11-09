# AGENTS.md — session checkpoint (2026-10-24, rewritten)

Working note for resuming this session's task. Not project documentation —
delete once the task below ships or is abandoned.

This file was rewritten on 2026-10-24. The previous revision described the
schematic as finished with `Q_en`/`R_dcen` wired as `Q17`-`Q20`/`R44`-`R47`;
that was not true of the file on disk. See "History worth knowing" below
before trusting any older note.

## Task

Implement `hardware/3pin_dc_mode_plan.md`: scope **S2** (all 4 fan channels),
runtime `mode` command, one shared PWM signal driving both `pwm` and `dc`
modes, with a per-channel static GPIO (`FANn_DCEN`) deciding whether the chop
reaches the motor rail. User picked S2 explicitly.

Topology per channel is plan section 3 (`Q_inv` + `Q_en` series cascode, PNP
gate lift, P-FET high-side switch). The user confirmed the cascode choice on
2026-10-24 after a pass-gate variant was found in the file and rejected.

## State

**Schematic: all 4 channels reworked and ERC-clean (0 errors, 0 warnings,
63 nets).** Uncommitted; `git status --short` shows only
`hardware/kicad/fan_controller.kicad_sch` modified.

**Firmware: the motor-switch work needs no change; the AIN reduction did.**
`TIM_OCM_PWM2` drives the shared gate high for `1 - CCR/period`, so `Q_hs`
conducts `CCR/period` — the same duty pin 4 carries, so the mode work is
polarity-correct with no firmware edit. Separately, dropping AIN5/AIN6
touched `board.h` (`AIN_COUNT` 4, pin-map comment), `thermal.c` (scan
sequence `{0,1,2,3,8,TEMP,VREF}`, analog GPIO mask `GPIO0..GPIO3`),
`tests/test_protocol.c` (expects `ain 4 raw=2051 mv=1653`) and
`host/fancontrold.py` (`AIN_COUNT = 4` plus a parse-time bound so a config
naming AIN5/AIN6 fails with `ConfigError` instead of a bare `KeyError`).
`make test` passes; `make` links at 11476 B text.

**PCB: not synced.** The net list changed substantially (per-channel rails
split, 20 parts added), so `sync_schematic_to_board` must run before anything
else on the PCB side. Standing instruction for that phase: MCP tools only,
try `autoroute` first, avoid moving existing placements.

## Pin map — schematic now matches `firmware/Core/board.h`

Both of these columns were wired to the wrong header rows before this session.
`J2`/`J3` pin numbers are header positions, not GPIO numbers; the Blue Pill
symbols' pin *names* carry the GPIO.

| net | MCU pin | header pin | note |
|---|---|---|---|
| FAN1_PWM_G | PB8 | J3/16 | TIM4_CH3 — `oc[]` crosses CH1/CH3 |
| FAN2_PWM_G | PB7 | J3/15 | TIM4_CH2 |
| FAN3_PWM_G | PB6 | J3/14 | TIM4_CH1 |
| FAN4_PWM_G | PB9 | J3/17 | TIM4_CH4 |
| FAN1_DCEN | PA6 | J2/10 | plain GPIO |
| FAN2_DCEN | PA7 | J2/9 | plain GPIO |
| FAN3_DCEN | PA8 | J3/5 | plain GPIO, the only one on J3 |
| FAN4_DCEN | PB1 | J2/7 | plain GPIO |
| V12_SENSE | PB0 | J2/8 | ADC1 IN8, R1/R2 divider + C4 |
| AIN1 | PA0 | J2/16 | header J11, R29, clamp D8 |
| AIN2 | PA1 | J2/15 | header J8, R30, clamp D9 |
| AIN3 | PA2 | J2/14 | header J9, R28, clamp D10 |
| AIN4 | PA3 | J2/13 | header J10, R27, clamp D6 |

Tach (`PB12`-`PB15` on J3/1-4) was already correct and was not touched.

AIN runs 1-4 only — the user cut AIN5/AIN6 outright ("we use only 4
channels"), which matches the four headers the board actually has. Each
chain is header → series 1 k → node with BAT54S clamp → MCU pin, numbered in
header order top to bottom. Surplus clamp `D7` and its orphaned `#PWR020`
GND symbol were deleted. `J8`-`J11` Value fields were renamed to match their
new AIN numbers, so the silkscreen follows the numbering.

## Per-channel parts and nets

| role | FAN1 | FAN2 | FAN3 | FAN4 |
|---|---|---|---|---|
| `Q_hs` AO3401A | Q1 | Q4 | Q7 | Q10 |
| `Q_inv` BSS138 | Q2 | Q5 | Q8 | Q11 |
| `Q_sig` BSS138 (fan pin 4) | Q3 | Q6 | Q9 | Q12 |
| `Q_en` BSS138 | Q13 | Q15 | Q17 | Q19 |
| `Q_pnp` MMBT3906 | Q14 | Q16 | Q18 | Q20 |
| `R_dcen` series 100 R | R3 | R9 | R15 | R21 |
| `R_gpd` 2.2 k | R4 | R10 | R16 | R22 |
| `R_pwm` series 100 R | R5 | R11 | R17 | R23 |
| gate pulldown 100 k | R6 | R12 | R18 | R24 |
| `R_b` 4.7 k | R31 | R34 | R37 | R40 |
| `R_be` 10 k | R32 | R35 | R38 | R41 |
| `R_dcen` pulldown 10 k | R33 | R36 | R39 | R42 |
| fuse / header / freewheel | F2/J4/D2 | F3/J5/D3 | F4/J6/D4 | F5/J7/D5 |

Nets per channel, all verified pin-by-pin with `get_net_connections`:
`+12V_Fn` (fuse out, `Q_hs` S, `Q_pnp` E, `R_be`), `FANn_12V` (`Q_hs` D,
freewheel K, header +), `FANn_HS_G` (`Q_hs` G, `R_gpd`, `Q_pnp` C),
`FANn_PNP_B` (`Q_pnp` B, `R_b`, `R_be`), `FANn_RB` (`Q_inv` D, `R_b`),
`FANn_INV_S` (`Q_inv` S, `Q_en` D), `FANn_DCEN_G` (`Q_en` G, series R,
pulldown), `FANn_PWM_GATE` (series R, pulldown, `Q_sig` G, `Q_inv` G),
`FANn_PWM_OUT` (`Q_sig` D, header pin 4).

## What was wrong before this session, and what fixed it

Everything below was found in commit `d9a9085` ("WIP") and is now repaired.
ERC reported **0 violations against all of it** — every fault was a legal net,
so ERC is not evidence this block is sound. Verify per-net instead.

1. **Both MCU columns off by rows.** `FANn_PWM_G` sat on PB3/PB4/PB5/PB6 and
   `FANn_DCEN` on PB1/PB0/PA7/PA6 in reverse order, with `FAN2_DCEN` landing
   on PB0 (the 12 V sense pin) and `V12_SENSE` on PA8. Fixed by moving the
   label/stub rows; `no_connect` flags moved accordingly (PB3/PB4/PB5 keep
   theirs, PB6-PB9 had theirs removed).
2. **Cross-channel merges were duplicate label names, not wire shorts.** Four
   labels all read `+12V_F4` and three read `FAN4_PWM_OUT`, so the four fused
   rails and three pin-4 outputs were single nets (the per-fan polyfuses ran
   in parallel). Fixed by renaming to `+12V_F1..F3` / `FANn_PWM_OUT`.
3. **12 V on an MCU pin.** A long vertical in each channel's lane terminated
   on `Q_hs`'s drain, merging the fan's 12 V output with the PWM gate node and
   so with the timer pin through a 100 R resistor. The lane x-coordinate was
   the same as the P-FET's drain/source column — the pin-coincidence gotcha
   below.
4. **`Q_hs` was never in series with anything.** The fuse output was wired
   straight to header pin 2; the P-FET's source went to `Q_inv`'s gate and its
   drain to the PWM node. `Q_sig`'s source sat on the 12 V rail instead of GND.
5. **No AND element existed.** `FANn_DCEN` drove the P-FET gate directly
   through a 100 R with a 100 k pulldown. Read charitably this was a P-FET
   pass-gate passing PWM to a low-side `Q_inv`, but the P-FET body diode
   (anode on drain) shorts PWM highs to the gate regardless of DCEN, so the
   enable could not work. The cascode has no such path.
6. **`R_gpd` was 100 k.** tau was about 40 us against a 40 us period. Now 2.2 k
   per the plan.

Resulting rest state, which is the C1 failsafe: both control lines high-Z,
`R6`-class pulldown holds the PWM gate low and `R_dcen` holds `Q_en` off, so
`Q_pnp` stays off, `R_gpd` holds `Q_hs` gate low, `Q_hs` conducts, fan runs at
full 12 V.

## History worth knowing

`d9a9085` ("WIP", 2026-10-24 22:23, authored outside this session) rewrote
`fan_controller.kicad_sch` (-13617/+9538) and **deleted `Q17`-`Q20` and
`R44`-`R47`** — the `Q_en`/`R_dcen` parts an earlier checkpoint claimed were
done. `JP1`-`JP4` are also gone, so the old "drop vs. keep the bypass jumper"
decision is moot. The `Q17`-`Q20`/`R37`-`R42` refs in use now were assigned
fresh this session and are unrelated to those deleted parts.

Also in that commit: an `erc_exclusion` in `fan_controller.kicad_pro` for
`pin_not_connected` at `914400|1409700` — that is PB7, which now carries
`FAN2_PWM_G`. The exclusion is dead weight; removing it is safe.

A `power:PWR_FLAG` (`#FLG01`, at 44.45/166.37, stubbed off the GND wire next
to `C4`) was added this session. Without it ERC reported "Input Power pin not
driven by any Output Power pins" on GND, because the net holds 29 `power:GND`
symbols and every connector/module GND pin is typed `passive`. No `PWR_FLAG`
had ever existed in this project.

## Open work

1. **BOM stale.** The BOM summary does not list the 20 added parts
   (`Q13`-`Q20`, `R31`-`R42`), the `R_gpd` value change, or the removal of
   `D7`. `README.md`'s pin map, analog section and architecture table were
   updated for AIN1-4, but it still needs the KiCad board-outline size,
   ERC/DRC counts, `hardware/datasheets/README.md` rows and `## TODO`
   removal — all of which were already blocked on final hardware.
2. **PCB sync**, as described under State. Note the PCB also still carries
   `D7` and `#PWR020`, both now deleted from the schematic.
3. **Plan doc drift.** `hardware/3pin_dc_mode_plan.md` section 7 still names
   `Q9`/`Q11`/`Q12`/`R40`-`R43` as the refs to add for channel 4; the actual
   assignments are the table above.
4. **`host/fancontrol.conf.example`** documents `sensor = 1` only, so it
   needs no edit, but any existing user config naming AIN5/AIN6 is now a
   `ConfigError`.

## Tooling gotchas

- **A wire connects to any pin whose exact coordinate its path crosses**,
  vertex or not, so a long lane that merely passes over a pin shorts to it.
  It does *not* connect to a wire it crosses without a coincident endpoint.
  This produced fault 3 above. Route every long run in its own lane and check
  the net after each add.
- **`batch_connect` exceeds the 30 s client timeout** on ~13-reference
  payloads. The write still lands, but the tail entries are silently dropped:
  channel 3 lost `D4/2` and channel 4 lost `Q12/2`, `J7/1`, `J7/2`, `D5/2`.
  There is no timeout parameter to raise. Never treat a timed-out
  `batch_connect` as complete — re-verify each net, and note ERC will catch
  only the dropped labels that leave a pin fully unconnected.
- **Never run two schematic/PCB-mutating MCP calls in parallel** against the
  same file. This once truncated `fan_controller.kicad_sch` to 0 bytes.
- `list_schematic_nets` omitted the GND net in one call while
  `get_net_connections GND` returned it. Prefer per-net queries when it
  matters.
- `Q_pnp` must use `Transistor_BJT:MMBT3906` (pins 1=B, 2=E, 3=C), not
  `Device:Q_PNP` — the generic symbol's pin numbers are letters and do not
  match the SOT-23 numeric pads.
- `delete_trace`/`query_traces` filtered by net name were unreliable on the
  PCB side; bounding-box queries and UUID-targeted deletes were the only
  methods that reflected live board state. `delete_component`/`move_component`
  take only a reference, so they cannot disambiguate duplicate-reference
  footprints — ask the user to resolve those in the GUI.
- On the `kicad` MCP server (KiCAD-MCP-Server, not Konnect): `autoroute` can
  run its Freerouting import through the file-based backend even while IPC
  shows connected and realtime, writing tracks/vias to the `.kicad_pcb` on
  disk without updating the PCB editor window already open on that board.
  `reload_board` and `open_board` both report success afterward but do not
  force the GUI to re-read the file — `get_board_info` kept reporting 0
  tracks over IPC until the user manually restarted KiCad (or use File →
  Revert). Treat a post-autoroute `get_board_info` of 0 tracks/vias as
  meaning the GUI is stale, not that routing failed — check the file
  directly (`grep -c "(segment" `/`"(via "` with no trailing space, since
  the token is followed by a newline) before concluding anything. Never let
  the GUI save in that stale state — it would overwrite the routed file.

## Standing instructions

- Connect with **global labels, not wires** (user, 2026-10-24). This reverses
  the earlier "real wires everywhere" instruction from 2026-10-23.
- For any hard or ambiguous call — placement, part substitution, routing
  conflict, DRC that will not clear — stop and ask rather than guess.
- Work one channel at a time and verify before replicating.
