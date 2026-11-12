#!/usr/bin/env bash
# Local mirror of .github/workflows/build.yml: runs the host unit tests
# (make -C test), then builds the firmware (make) and the simulator
# (emmake make) for every board x display combination.
#
#   scripts/ci-local.sh                         # full matrix (the PR gate)
#   scripts/ci-local.sh sensorwatch_red custom  # one combination
#
# Without arm-none-eabi-gcc and emmake on PATH, it re-runs itself inside the
# flake's dev shell (flake.nix).
set -euo pipefail

# readlink -f: run through a symlink, the repo is still found from the real file.
self="$(readlink -f "$0")"
cd "$(dirname "$self")/.."

if ! command -v arm-none-eabi-gcc >/dev/null || ! command -v emmake >/dev/null; then
  if [[ -n "${CI_LOCAL_IN_NIX:-}" ]]; then
    echo "error: toolchain still missing inside nix develop" >&2
    exit 1
  fi
  export CI_LOCAL_IN_NIX=1
  exec nix --extra-experimental-features 'nix-command flakes' develop -c "$self" "$@"
fi

if [[ -n "$(git submodule status --recursive | { grep '^-' || true; })" ]]; then
  echo "error: submodules not initialized; run: git submodule update --init --recursive" >&2
  exit 1
fi

boards=(sensorwatch_red sensorwatch_green sensorwatch_blue sensorwatch_pro)
displays=(classic custom)
if [[ $# -gt 2 ]]; then
  echo "usage: $0 [BOARD [DISPLAY]]" >&2
  exit 2
fi
# check <value> <allowed...>: exit unless <value> is one of <allowed>.
check() {
  local value="$1" a
  shift
  for a in "$@"; do [[ "$value" == "$a" ]] && return 0; done
  echo "error: '$value' is not one of: $*" >&2
  exit 2
}
if [[ $# -ge 1 ]]; then check "$1" "${boards[@]}"; boards=("$1"); fi
if [[ $# -ge 2 ]]; then check "$2" "${displays[@]}"; displays=("$2"); fi

# Logs are kept only when something failed; the failing step prints its log path.
logs="$(mktemp -d "${TMPDIR:-/tmp}/ci-local.XXXXXX")"
trap 'status=$?; if [[ $status -eq 0 ]]; then rm -rf "$logs"; else echo "logs kept in $logs" >&2; fi' EXIT
ncpu="$(nproc)"

need() { # need <file...>: fail loudly if a build artifact is missing
  local f
  for f in "$@"; do
    [[ -f "$f" ]] || { echo "FAIL  missing artifact $f" >&2; exit 1; }
  done
}

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

# Host unit tests (the host-tests job): plain C with the PC's compiler.
run "host-tests" make -C test

for board in "${boards[@]}"; do
  for display in "${displays[@]}"; do
    # Clean between combinations so no objects carry over between boards.
    rm -rf build build-sim
    # BOARD/DISPLAY go on the command line: an X11 DISPLAY in the
    # environment must never leak into make.
    run "firmware-$board-$display" make -j"$ncpu" BOARD="$board" DISPLAY="$display"
    need build/firmware.uf2
    # Serial, like hosted CI: a parallel emscripten build intermittently
    # failed to link (`undefined symbol: free`).
    # -UEMSCRIPTEN: newer emsdk (hosted CI) no longer predefines the legacy
    # EMSCRIPTEN macro, but the flake's emcc still does. Code must test
    # __EMSCRIPTEN__.
    run "simulator-$board-$display" env EMCC_CFLAGS="${EMCC_CFLAGS:-} -UEMSCRIPTEN" \
      emmake make BOARD="$board" DISPLAY="$display"
    need build-sim/firmware.html build-sim/firmware.wasm build-sim/firmware.js
  done
done

echo "all combinations passed"
