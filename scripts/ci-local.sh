#!/usr/bin/env bash
# Local mirror of .github/workflows/build.yml: builds the firmware (make) and
# the simulator (emmake make) for every board x display combination.
#
#   scripts/ci-local.sh                         # full matrix (the PR gate)
#   scripts/ci-local.sh sensorwatch_red custom  # one combination
#
# Without arm-none-eabi-gcc and emmake on PATH, it re-runs itself inside the
# flake's dev shell (flake.nix).
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v arm-none-eabi-gcc >/dev/null || ! command -v emmake >/dev/null; then
  if [[ -n "${CI_LOCAL_IN_NIX:-}" ]]; then
    echo "error: toolchain still missing inside nix develop" >&2
    exit 1
  fi
  export CI_LOCAL_IN_NIX=1
  exec nix --extra-experimental-features 'nix-command flakes' develop -c "$0" "$@"
fi

if git submodule status | grep -q '^-'; then
  echo "error: submodules not initialized; run: git submodule update --init --recursive" >&2
  exit 1
fi

boards=(sensorwatch_red sensorwatch_green sensorwatch_blue sensorwatch_pro)
displays=(classic custom)
if [[ $# -ge 1 ]]; then boards=("$1"); fi
if [[ $# -ge 2 ]]; then displays=("$2"); fi

logs="${TMPDIR:-/tmp}/ci-local"
mkdir -p "$logs"
jobs="$(nproc)"

run() { # run <label> <command...>
  local label="$1" log="$logs/$1.log"
  shift
  if "$@" >"$log" 2>&1; then
    echo "ok    $label"
  else
    echo "FAIL  $label (log: $log)" >&2
    tail -n 30 "$log" >&2
    exit 1
  fi
}

for board in "${boards[@]}"; do
  for display in "${displays[@]}"; do
    # Clean between combinations so no objects carry over between boards.
    rm -rf build build-sim
    # BOARD/DISPLAY go on the command line: an X11 DISPLAY in the
    # environment must never leak into make.
    run "firmware-$board-$display" make -j"$jobs" BOARD="$board" DISPLAY="$display"
    test -f build/firmware.uf2
    # Serial, like hosted CI: a parallel emscripten build intermittently
    # failed to link (`undefined symbol: free`).
    run "simulator-$board-$display" emmake make BOARD="$board" DISPLAY="$display"
    test -f build-sim/firmware.html
  done
done

echo "all combinations passed"
