#!/usr/bin/env bash
# Renders every printable part to stl/. Usage: ./build.sh [part…]
set -euo pipefail
cd "$(dirname "$0")"
parts=("$@")
[ ${#parts[@]} -eq 0 ] && parts=(shoulder-mount upper-arm-top upper-arm-bottom forearm-top forearm-bottom couplers)
mkdir -p stl
for p in "${parts[@]}"; do
  echo "== $p"
  openscad -o "stl/$p.stl" "$p.scad"
done
