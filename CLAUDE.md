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
| `template/` | Face template and the `watch_face.py` generator |
| `gossamer/`, `littlefs/`, `tinyusb/`, `utz/` | Git submodules. Never edit them. |
| `legacy/` | Old Movement faces that aren't ported yet. They aren't built. |

## Toolchain

You need `arm-none-eabi-gcc` (firmware), `emcc`/`emmake` (simulator) and
GNU make. `flake.nix` provides all three:

```bash
nix --extra-experimental-features 'nix-command flakes' develop   # or direnv: .envrc runs `use flake`
```

`.devcontainer/` is the alternative. On Debian or Ubuntu,
`apt install gcc-arm-none-eabi` plus an emsdk install also works.

Submodules must be checked out:

```bash
git clone --recurse-submodules <url>        # fresh clone
git submodule update --init --recursive     # existing clone or new worktree
```

## Build

```bash
make BOARD=sensorwatch_red DISPLAY=custom        # firmware → build/firmware.uf2
emmake make BOARD=sensorwatch_red DISPLAY=custom # simulator → build-sim/firmware.html
python3 -m http.server -d build-sim              # open http://localhost:8000/firmware.html
```

- `BOARD`: `sensorwatch_red` (Sensor Watch Lite), `sensorwatch_green`,
  `sensorwatch_blue`, `sensorwatch_pro`.
- `DISPLAY`: `classic` or `custom`. `autodetect` exists but the Makefile
  warns it's unreliable, so don't use it. Always put `DISPLAY` on the
  command line. An X11 `DISPLAY=:0` in the environment would otherwise
  cause an "invalid DISPLAY type" error.
- `TIMESET=year|day|minute` bakes the host clock into the firmware.
  `NOSLEEP=1` disables low-energy mode.
- Run `make clean` (or `rm -rf build build-sim`) when you switch `BOARD` or
  `DISPLAY`.

## CI commands

`.github/workflows/build.yml` builds firmware and simulator for 4 boards ×
2 displays. The local mirror is:

```bash
scripts/ci-local.sh                         # full matrix, 16 builds: the PR gate
scripts/ci-local.sh sensorwatch_red custom  # one combination, for iterating
```

The script enters the nix shell on its own and stops at the first failing
build. There's no lint step, and no unit tests until #15 lands (see
[Testing](#testing)).

## Adding a face

1. Generate the files. Run this from `template/`, since its paths are
   relative:
   ```bash
   cd template && python3 watch_face.py complication my_thing --author-name "Your Name"
   ```
   This creates `watch-faces/complication/my_thing_face.{c,h}`. It also
   adds the `#include` above `// New includes go above this line.` in
   `movement_faces.h`, and the `.c` above `# New watch faces go above this
   line.` in `watch-faces.mk`. The types are `clock`, `complication`, `demo`,
   `sensor` and `settings`. For an `io` face, add those lines by hand.
2. Add `my_thing_face` to `watch_faces[]` in `movement_config.h`. The
   generator doesn't do this step.
3. Mind the secondary faces. `MOVEMENT_SECONDARY_FACE_INDEX` is
   `MOVEMENT_NUM_FACES - 5`, so the **last 5 entries** in `watch_faces[]`
   are left out of the normal MODE rotation. They're reached by
   long-pressing MODE on the first face. Insert a regular face before those
   five.
4. Build. Enabling more faces than the watch can store fails at compile
   time, so remove a face if that happens.

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
  (`filesystem.h`). Both faces have their own editor for it.

## Flashing

You do this step by hand on the hardware, so it's a `manual-task`, not
something an agent can verify. Plug in the watch and double-tap the reset
button on the back. The LED pulses red and a `WATCHBOOT` drive appears. Then
run `make install`, or copy `build/firmware.uf2` onto `WATCHBOOT`.

## Branches and remotes

- `main` holds shared changes. **All PRs target `main`.**
- `KnightConfig` is the personal build for the red board with the custom
  display: its own face list and timer defaults. The owner merges `main`
  into it. Never rebase it, push to it or open PRs against it.
- `origin` is `swamp-dev/second-movement`. The upstream is
  `joeycastillo/second-movement`, and this fork is about 292 commits behind
  it as of 2026-11. To compare:
  ```bash
  git remote add upstream https://github.com/joeycastillo/second-movement.git
  git fetch upstream && git log --oneline main..upstream/main
  ```
  Upstream syncs are the owner's call. Merge, don't rebase, and never sync
  as part of a ticket.

## Testing

There's no test framework yet. For now:
- `scripts/ci-local.sh` must pass.
- Check behaviour changes in the simulator (`emmake make …`, then the
  browser).
- Check on the watch only through a `manual-task`.

Issue #15 adds host-compiled unit tests for pure logic (planetary hour and
zodiac calculations). Once it lands, logic changes use TDD against them, and
this section and `scripts/ci-local.sh` get updated.
