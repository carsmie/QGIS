---
name: cross-platform-requirement
description: "Kartverket GDAL/QGIS work must work on Windows and Linux, not just the user's Mac"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 260d7884-cd4d-4e63-8737-785263ada196
  modified: 2026-09-28T10:43:48.944Z
---

The user develops on macOS, but everything (GDAL patches, QGIS changes, build setups) must also work on Windows and Linux.

**Why:** said explicitly on 2026-09-28 while building QGIS against the patched GDAL ([[gdal-fork-rect-filter]]); production/other users run Linux and Windows.

**How to apply:** keep code changes platform-neutral (no macOS-only APIs, watch compiler differences: MSVC, GCC's default -ffp-contract=fast, 32-bit x87). Treat Homebrew/zsh/dyld workarounds as local dev setup only and say so; verify on Linux (e.g. a container) and rely on/offer CI for Windows before calling something done.
