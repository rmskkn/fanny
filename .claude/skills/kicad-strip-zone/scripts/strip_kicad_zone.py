#!/usr/bin/env python3
"""Remove copper zone blocks for given net(s) from a KiCad .kicad_pcb file.

Dry run by default: lists matching zone blocks without touching the file.
Pass --apply to actually rewrite the file (a .bak copy is always made first).

Why this exists: the kicad MCP server's delete_graphic tool only handles
graphic/drawing items, not zone items, and there is no dedicated delete_zone
tool. Zone blocks are also often huge (thousands of polygon points), so
reading one into a chat/agent context to edit it by hand is wasteful. This
script finds each top-level "(zone" block by paren-depth matching and splices
it out at the line level, so the polygon data never needs to be read.

Usage:
    strip_kicad_zone.py <board.kicad_pcb> --net NET [--net NET ...] [--apply]

Example:
    strip_kicad_zone.py fan_controller.kicad_pcb --net GND --apply
"""
import argparse
import shutil
import sys


def find_zone_blocks(lines):
    """Return (start, end, text) for every top-level "(zone" block, 0-indexed inclusive."""
    blocks = []
    for start, line in enumerate(lines):
        if line.strip() != "(zone":
            continue
        depth = 0
        end = None
        for i in range(start, len(lines)):
            for ch in lines[i]:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end is not None:
                break
        if end is None:
            raise ValueError(f"unbalanced parens in zone block starting at line {start + 1}")
        blocks.append((start, end))
    return blocks


def block_nets(lines, start, end):
    """Net names referenced by a zone block's (net "...") line(s)."""
    nets = []
    for i in range(start, end + 1):
        stripped = lines[i].strip()
        if stripped.startswith('(net "') and stripped.endswith('")'):
            nets.append(stripped[len('(net "'):-len('")')])
    return nets


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pcb_path", help="path to the .kicad_pcb file")
    parser.add_argument("--net", action="append", required=True, dest="nets",
                         help="net name to strip zones for; repeat for multiple nets")
    parser.add_argument("--apply", action="store_true",
                         help="actually rewrite the file (default: dry run, just report)")
    args = parser.parse_args()

    with open(args.pcb_path, "r") as f:
        lines = f.readlines()

    blocks = find_zone_blocks(lines)
    if not blocks:
        print("no top-level (zone blocks found in file")
        return

    target_nets = set(args.nets)
    matches = []
    for start, end in blocks:
        nets = block_nets(lines, start, end)
        hit = target_nets & set(nets)
        if hit:
            matches.append((start, end, nets))

    if not matches:
        print(f"no zone blocks found for net(s): {', '.join(sorted(target_nets))}")
        print(f"({len(blocks)} zone block(s) total in file, none match)")
        return

    for start, end, nets in matches:
        print(f"zone lines {start + 1}-{end + 1}: net(s) {nets}")

    if not args.apply:
        print(f"\ndry run: {len(matches)} matching block(s) found, none removed. Pass --apply to write.")
        return

    backup_path = args.pcb_path + ".bak"
    shutil.copy2(args.pcb_path, backup_path)

    skip = set()
    for start, end, _ in matches:
        skip.update(range(start, end + 1))
    kept = [line for i, line in enumerate(lines) if i not in skip]

    with open(args.pcb_path, "w") as f:
        f.writelines(kept)

    print(f"\nremoved {len(matches)} zone block(s) for net(s) {', '.join(sorted(target_nets))}")
    print(f"backup written to {backup_path}")
    print("remember: the removed copper pour may have carried real connectivity (e.g. thermal-relief "
          "stubs that were never routed point-to-point). Run DRC after reopening the board and expect "
          "to re-route the affected net with real traces.")


if __name__ == "__main__":
    sys.exit(main())
