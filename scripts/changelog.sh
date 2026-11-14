#!/usr/bin/env bash
# List commits as "<short-hash> <title>" for a release change list.
#
# Usage: scripts/changelog.sh [<from>] [<to>]
#   <to>   defaults to HEAD
#   <from> defaults to the nearest tag reachable from <to>'s parent,
#          so with no args this lists everything since the last tag.
set -euo pipefail

to="${2:-HEAD}"
from="${1:-$(git describe --tags --abbrev=0 "$to^" 2>/dev/null || true)}"

range="$to"
if [ -n "$from" ]; then
	range="$from..$to"
fi

git log --no-merges --pretty=format:'%h %s' "$range"
