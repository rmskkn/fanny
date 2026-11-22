---
name: release
description: Cut a tagged release for the fanny repo (firmware + hardware). Use whenever the user asks to prepare, cut, tag, or ship a release, or names a version like "1.0.1", "v1.0.1", or "next release" — tag push is the only trigger, there is no separate version-bump step or CHANGELOG to edit.
---

# Release (fanny)

A release here is just an annotated git tag pushed to `origin`. CI does
the rest: builds firmware, exports gerbers, and attaches both to a
GitHub release. There is no version file to bump and no changelog file
to write — don't go looking for one.

## What a tag triggers

`.github/workflows/release.yml` runs on `push: tags: "v*"`:

1. `ci` job calls `firmware.yml` (build)
2. `gerbers` job calls `gerbers.yml` (export)
3. `release` job (needs both) checks out the repo with submodules,
   downloads the `fanny-firmware` and `fanny-gerbers` artifacts and
   publishes a GitHub release via `softprops/action-gh-release@v2`,
   attaching:
   - `firmware/build/fanny.elf`
   - `firmware/build/fanny.bin`
   - `hardware/gerbers/fanny-gerbers.zip`
   - `hardware/case/fan_controller_tray.stl` and
     `fan_controller_cover.stl`, the committed files as they are (CI
     doesn't run FreeCAD), so they must be current before tagging
   - `LICENSE`, `LICENSE-GPL-3.0` and libopencm3's `COPYING.LGPL3`

## Steps

1. Confirm the working tree is clean and the commit you want released
   is on `main` and already pushed (`git log --oneline -1`,
   `git status --short`).
2. Pick the next version. Past tags: `v0.0.1`–`v0.0.9`, `v1.0.0`. Bump
   patch for fixes, minor for new features, major for breaking
   changes — but if the user names an exact version, use that one even
   if it doesn't match semver strictly; it's their call.
3. Tag and push:
   ```
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```
4. Watch the workflow: `gh run watch` (or `gh run list --workflow=release.yml`
   to find the run id first if it hasn't picked it up yet).
5. Verify: `gh release view vX.Y.Z` should show the three assets above.

## Things that look like they need touching but don't

- `firmware/App/protocol.c` has `#define FW_VERSION "fanctl 2.1"` — this
  is the wire protocol version the host tool checks, unrelated to the
  release tag. Only bump it if the protocol itself changed.
- No `CHANGELOG.md` exists. Release bodies on past tags are empty; the
  GitHub release name/body default to the bare tag name. Don't write
  release notes unless the user asks for them.
