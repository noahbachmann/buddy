#!/usr/bin/env bash
# Renders every printable part to stl/. Usage: ./build.sh [part…]
# Needs the pinned toolchain: parts/tools/setup.sh (OpenSCAD nightly, Manifold backend).
set -euo pipefail
cd "$(dirname "$0")"
export OPENSCADPATH=$PWD/../lib
parts=("$@")
[ ${#parts[@]} -eq 0 ] && parts=(shoulder-mount upper-arm-top upper-arm-bottom forearm-top forearm-bottom couplers)
mkdir -p stl
for p in "${parts[@]}"; do
  echo "== $p"
  openscad --backend=manifold --export-format=binstl -q -o "stl/$p.stl" "$p.scad"
done
