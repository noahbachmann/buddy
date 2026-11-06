#!/usr/bin/env bash
# Installs the pinned CAD toolchain for parts/. Idempotent: safe to rerun, only fetches what's missing.
# Usage: parts/tools/setup.sh            (local machine and CI)
# Puts tools in ~/.local/opt/<name>-<version>, links them into ~/.local/bin.
set -euo pipefail

OPENSCAD_VERSION=2026.10.22   # nightly snapshot: Manifold backend, PNG export
OPT=${CAD_OPT:-$HOME/.local/opt}
BIN=${CAD_BIN:-$HOME/.local/bin}
here=$(cd "$(dirname "$0")" && pwd)
parts=$(dirname "$here")

mkdir -p "$OPT" "$BIN"

# --- OpenSCAD ---------------------------------------------------------------------
# Extracted, not run as AppImage: Ubuntu 24.04 ships no libfuse2.
scad_dir=$OPT/openscad-$OPENSCAD_VERSION
if [ ! -x "$scad_dir/AppRun" ]; then
  echo "== OpenSCAD $OPENSCAD_VERSION"
  tmp=$(mktemp -d)
  curl -fL --progress-bar -o "$tmp/openscad.AppImage" \
    "https://files.openscad.org/snapshots/OpenSCAD-$OPENSCAD_VERSION-x86_64.AppImage"
  chmod +x "$tmp/openscad.AppImage"
  (cd "$tmp" && ./openscad.AppImage --appimage-extract >/dev/null)
  rm -rf "$scad_dir" && mv "$tmp/squashfs-root" "$scad_dir"
  rm -rf "$tmp"
fi
ln -sfn "$scad_dir/AppRun" "$BIN/openscad"

# --- OpenSCAD libraries (pinned submodules) ------------------------------------------
git -C "$parts" submodule update --init --recursive lib/BOSL2 lib/NopSCADlib

# --- Python tools ---------------------------------------------------------------------
uv sync --project "$here" --quiet

echo "== ok"
"$BIN/openscad" --version
