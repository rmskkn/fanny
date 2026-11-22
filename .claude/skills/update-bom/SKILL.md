---
name: update-bom
description: Sync hardware/bom.csv with the schematic after any schematic or PCB change (component added, removed, or swapped). Use whenever a schematic/PCB edit lands, before committing hardware changes, or when asked to update/sync/check the BOM.
---

# Keep hardware/bom.csv in sync with the schematic

`hardware/bom.csv` is **hand-curated**, not a `kicad-happy` bom-skill export.
Rows group references by value (`R9 R20 R29 R36 R43 R44 R45 R46`), carry
hand-written `Notes` (part-swap history, lifecycle warnings, vendor quirks),
and use project-specific column names (`MPN (example)`, `DigiKey PN`,
`DigiKey URL`). Do not run `bom_manager.py export` over this file — it
produces one row per reference with its own column set and would flatten
the grouping and wipe the Notes.

## Workflow

1. Diff schematic references against `bom.csv` references:

   ```bash
   python3 - <<'EOF'
   import re, csv
   sch = open("hardware/kicad/fan_controller.kicad_sch").read()
   refs = set(re.findall(r'\(property "Reference" "([^"]+)"', sch))
   # library-template placeholders (single letter, no digit) aren't real parts
   refs = {r for r in refs if not r.startswith('#') and len(r) > 1 and any(c.isdigit() for c in r)}
   bom_refs = set()
   with open("hardware/bom.csv") as f:
       for row in csv.DictReader(f):
           bom_refs.update(row["References"].split())
   print("in schematic, missing from bom.csv:", sorted(refs - bom_refs))
   print("in bom.csv, missing from schematic:", sorted(bom_refs - refs))
   EOF
   ```

2. **Removed component**: delete its reference from the `References` cell.
   If it shared a grouped row with others (e.g. `F2 F3 F4 F5`), just drop the
   removed ref(s) and fix `Qty` to match; if the row's refs were all removed,
   delete the whole row. If it had the only instance of a part, delete the
   row.
3. **New component**: add a row (or extend an existing matching-value row's
   `References`/`Qty`) with `Value`, `Footprint`, and sourcing info. Look up
   `MPN (example)` / `DigiKey PN` / `DigiKey URL` via the `digikey` skill
   (or `bom`/`kicad-happy:bom` skill for other distributors) rather than
   guessing a part number.
4. **Swapped part** (same reference, different value/footprint/MPN): update
   that row in place and add a one-line `Notes` entry describing the change
   (see existing rows like J8-J11 and D1 for the expected style: what it is
   now, what it replaced).
5. Re-run the diff script from step 1 — it must print two empty lists before
   the BOM change is done.

## PCB-only parts are a known, separate gap

`MH1-4` (mounting holes), `FB1`, `C6-C9` exist on the PCB but not the
schematic, and are **not** tracked in `bom.csv` (mounting holes legitimately
never belong in a BOM; `FB1`/`C6-C9` are a pre-existing gap, not something
introduced by a given change). The diff script only compares
schematic-vs-BOM, so it won't flag these — don't try to make it match PCB
footprint count.
