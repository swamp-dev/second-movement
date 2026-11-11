# second-movement

Firmware for [Sensor Watch](https://www.sensorwatch.net) (C, SAM L22 MCU).
This repo is a fork of `joeycastillo/second-movement`, and most changes are
new or fixed watch faces.

**Process:** [`docs/workflow.md`](docs/workflow.md) is the source of truth for
how a ticket becomes a merged PR. Read it before starting work.

## Layout

| Path | What |
|---|---|
| `watch-faces/<clock\|complication\|demo\|io\|sensor\|settings>/` | Watch faces, one `<name>_face.c/.h` pair each |
| `movement.c`, `movement.h` | Face loop, events, settings |
| `movement_faces.h`, `watch-faces.mk`, `movement_config.h` | Face registration (see [Adding a face](#adding-a-face)) |
| `watch-library/shared/watch/` | Hardware API (`watch_slcd.h` for the display, `watch_rtc.h`, …) |
| `lib/` | Shared helpers (`sunriset`, `TOTP`, `base32`, …) |
| `test/` | Host unit tests (`make -C test`, see [Testing](#testing)) |
| `filesystem/`, `location/` | Stored settings and the shared location editor |
| `template/` | Face template and the `watch_face.py` generator |
| `gossamer/`, `littlefs/`, `tinyusb/`, `utz/` | Git submodules. Never edit them. |
| `legacy/` | Old Movement faces that aren't ported yet. They aren't built. |

## Toolchain

You need `arm-none-eabi-gcc` (firmware), `emcc`/`emmake` (simulator), GNU
make, and `python3`, which the build uses to run `gossamer/utils/uf2/uf2conv.py`.
`flake.nix` provides all four. If `command -v arm-none-eabi-gcc emmake`
doesn't print two paths, prefix every build command with
`nix --extra-experimental-features 'nix-command flakes' develop -c`. Each Bash
call is a new shell, so an interactive `nix develop` (or direnv's
`use flake`) doesn't carry over. `.devcontainer/` is an alternative.

Submodules must be checked out:

```bash
git clone --recurse-submodules <url>        # fresh clone
git submodule update --init --recursive     # existing clone or new worktree
```

## Build

```bash
nix=(nix --extra-experimental-features 'nix-command flakes' develop -c)   # drop if the toolchain is on PATH
"${nix[@]}" make BOARD=sensorwatch_red DISPLAY=custom        # firmware → build/firmware.uf2
"${nix[@]}" emmake make BOARD=sensorwatch_red DISPLAY=custom # simulator → build-sim/firmware.html
python3 -m http.server -d build-sim   # in the background; open http://localhost:8000/firmware.html
```

A firmware build worked if it ends with the `size` table and
`build/firmware.uf2` exists.

- `BOARD`: `sensorwatch_red` (Sensor Watch Lite), `sensorwatch_green`,
  `sensorwatch_blue`, `sensorwatch_pro`.
- `DISPLAY`: `classic` or `custom` (the Makefile warns that `autodetect` is
  unreliable). Always pass it on the command line. An X11 `DISPLAY=:0` from
  the environment fails with "invalid DISPLAY type".
- `TIMESET=year|day|minute` bakes the host clock into the firmware.
  `NOSLEEP=1` disables low-energy mode.
- Run `rm -rf build build-sim` when you switch `BOARD` or `DISPLAY`.
  `make clean` removes only `build/`.

## CI commands

`.github/workflows/build.yml` runs the host unit tests (`host-tests`) and
builds firmware and simulator for 4 boards × 2 displays. The local mirror is:

```bash
scripts/ci-local.sh                         # host tests + 16 builds: the PR gate
scripts/ci-local.sh sensorwatch_red custom  # host tests + one combination, for iterating
make -C test                                # host tests only
```

The script enters the nix shell on its own, runs the host tests first, wipes
`build/` and `build-sim/`, and stops at the first failure. There's no lint
step. Local builds use the flake's GCC 14, while hosted CI uses the older
GCC in its `mbed-os-env:latest` image. GCC 14 is stricter, so a local pass
and a hosted pass are separate checks, and both are required.

## Adding a face

1. Generate the files. The script's paths are relative to `template/`, so
   run it in a subshell:
   ```bash
   (cd template && python3 watch_face.py complication my_thing --author-name "<your name>")
   ```
   It worked only if it prints `Successfully generated`. It exits 0 even
   when it fails, so if that line is missing, run `git diff` and revert any
   half-made edit.
   This creates `watch-faces/complication/my_thing_face.{c,h}`. It also
   adds the `#include` above `// New includes go above this line.` in
   `movement_faces.h`, and the `.c` above `# New watch faces go above this
   line.` in `watch-faces.mk`. The types are `clock`, `complication`, `demo`,
   `sensor` and `settings`. For an `io` face, generate it as `complication`, move both files to
   `watch-faces/io/`, and fix the path in `watch-faces.mk`.
2. Add `my_thing_face` to `watch_faces[]` in `movement_config.h`. The
   generator doesn't do this step.
3. Mind the secondary faces. `MOVEMENT_SECONDARY_FACE_INDEX` is
   `MOVEMENT_NUM_FACES - 6`, so the **last 6 entries** in `watch_faces[]`
   are left out of the normal MODE rotation. They're reached by
   long-pressing MODE on the first face. Insert a regular face before those
   six.
4. Build with `scripts/ci-local.sh sensorwatch_red custom`. If the link fails
   with "region `flash' overflowed", remove a face from `watch_faces[]`.

## Gotchas

- **Display width differs by LCD.** `WATCH_POSITION_TOP` is 2 characters on
  `classic` and 5 on `custom`. Use
  `watch_display_text_with_fallback(pos, long, short)` and check that both
  strings make sense (see `watch_slcd.h`). `watch_display_string` is
  deprecated.
- **`timer_face` defaults are packed little-endian:** byte0 = hours,
  byte1 = minutes, byte2 = seconds. For example, `0x002D02` is 2 h 45 m
  (`_default_timer_values` in `watch-faces/complication/timer_face.c`).
- Faces that need a location (`planetary_hour_face`, `sunrise_sunset_face`)
  share one stored location through `load_location_from_filesystem()`
  (`filesystem/filesystem.h`). Both faces have their own editor for it.

## Flashing

Only the user can flash the watch, so put these steps in a `manual-task`
issue. Plug in the watch and double-tap the reset
button on the back. The LED pulses red and a `WATCHBOOT` drive appears. Then
run `make install`, or copy `build/firmware.uf2` onto `WATCHBOOT`.

## Branches and remotes

- `main` holds shared changes. **All PRs target `main`.**
- `KnightConfig` is the personal build for the red board with the custom
  display: its own face list and timer defaults. The owner merges `main`
  into it, and agents never touch it
  ([workflow](docs/workflow.md#branches-worktrees-commits)).
- `origin` is `swamp-dev/second-movement`. The upstream is
  `joeycastillo/second-movement`. To compare:
  ```bash
  git remote add upstream https://github.com/joeycastillo/second-movement.git
  git fetch upstream && git log --oneline main..upstream/main
  ```
  Upstream syncs are the owner's call. Merge, don't rebase, and never sync
  as part of a ticket.

## Testing

Pure logic lives in `lib/` and is unit-tested on the PC by `make -C test`
(plain `cc`, no ARM toolchain). To add a test, write `test/test_<name>.c`
using the `CHECK` macros in `test/check.h`, and end its `main` with
`return check_summary("test_<name>");`. Then add `test_<name>` to `TESTS`
and its sources to `test_<name>_SRCS` in `test/Makefile`. A new
`lib/<dir>` also needs `-I../lib/<dir>` in that file's `CPPFLAGS`, and its
`-I` and `.c` lines in the root `Makefile`. Tests build with
AddressSanitizer and UBSan, and any failure fails the run. What to check for
each kind of change is in [`docs/workflow.md` § Testing](docs/workflow.md#testing).
