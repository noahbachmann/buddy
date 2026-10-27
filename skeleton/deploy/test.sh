#!/usr/bin/env bash
# All tests: C++ (GoogleTest via ctest, under ASan + UBSan), Python (pytest), shell scripts (shellcheck).
#
# Usage: deploy/test.sh [ctest args…]          here (VM/PC); e.g. deploy/test.sh -R Player
#        deploy/test.sh --pi [ctest args…]     C++ tests on the Pi (aarch64, GCC 14): sync, build in
#                                              ~/buddy/build-test, ctest. Touches no running service.
#
# Env: BUDDY_HOST (default "buddy")
set -euo pipefail

HOST="${BUDDY_HOST:-buddy}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # project root = parent of deploy/
# Only the test executables get built (target buddy-tests): they link no ML libraries, so this stays fast.
CMAKE_ARGS=(-G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUDDY_SANITIZE=ON)

if [[ "${1:-}" == --pi ]]; then
    shift
    "$ROOT/deploy/sync.sh" > /dev/null
    echo "→ C++ tests on $HOST"
    ssh "$HOST" bash -s -- "${CMAKE_ARGS[*]}" "$@" <<'REMOTE'
set -euo pipefail
cmake_args="$1"; shift
cd ~/buddy
# shellcheck disable=SC2086  # word splitting wanted
cmake -S src -B build-test $cmake_args -DCMAKE_CXX_COMPILER_LAUNCHER=ccache > /dev/null
cmake --build build-test --target buddy-tests
ctest --test-dir build-test --output-on-failure -j "$(nproc)" "$@"
REMOTE
    exit
fi

failed=()
echo "→ C++"
cmake -S "$ROOT" -B "$ROOT/build-test" "${CMAKE_ARGS[@]}" > /dev/null
cmake --build "$ROOT/build-test" --target buddy-tests
ctest --test-dir "$ROOT/build-test" --output-on-failure -j "$(nproc)" "$@" || failed+=(C++)
echo "→ Python"
uv run --project "$ROOT" pytest -q --rootdir "$ROOT" "$ROOT" || failed+=(Python)
echo "→ shellcheck"
shellcheck "$ROOT"/deploy/*.sh "$ROOT"/deploy/pi/*.sh || failed+=(shellcheck)

if [[ ${#failed[@]} -gt 0 ]]; then
    echo "✗ failed: ${failed[*]}" >&2
    exit 1
fi
echo "✓ all tests passed"
