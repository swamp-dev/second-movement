#!/usr/bin/env python3
"""Capture watch-face screens as PNGs, for visual review.

    scripts/lcd-shots.py --root <checkout> --out <new dir> [face ...]

For each face with a test/shots/shot_<face>.c in <checkout>, builds and runs
that program (host compiler, face test harness), then paints every screen it
reports onto the simulator's LCD artwork (watch-library/simulator/shell.html)
and writes <out>/<face>/<classic|custom>/<shot>.png. With no face named, it
captures them all. A named face with no shot file in <checkout> is skipped, so
the same command captures a base commit from before the face had shots.

Needs make and a C compiler, Pillow, and Chrome or Chromium (set CHROME to
choose the binary). The script that runs is this one; the faces, the harness
and the artwork come from <checkout>.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

NAME = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
LCDS = ("classic", "custom")
# One screen in the sheet Chrome renders: a caption above the LCD.
CELL_W, CAPTION_H, COLUMNS = 720, 28, 4
# The glass, in the artwork's own units: the rect the backlight fills.
GLASS = re.compile(r'<g id="light"[^>]*>\s*<rect x="([\d.]+)" y="([\d.]+)" width="([\d.]+)" height="([\d.]+)"')
# Unlit segments stay faintly visible, as on the real glass, so a reviewer can
# see which segments a shape had to choose from. The simulator hides them.
# They are one faint copy of the whole LCD under the lit segments, because the
# artwork draws some shapes twice and two faint layers would add up.
GHOST_OPACITY = 0.12

PAGE = """<!doctype html><meta charset="utf-8">
<style>
  body {{ margin: 0; background: #fff; width: {page_w}px; }}
  .cell {{ float: left; width: {cell_w}px; height: {cell_h}px; overflow: hidden; }}
  .caption {{ height: {caption_h}px; line-height: {caption_h}px; padding-left: 8px;
              font: 14px/{caption_h}px monospace; color: #000; background: #fff; }}
  svg {{ display: block; }}
</style>
<body>
<template id="watch">{svg}</template>
<script>
  const shots = {shots};
  for (const s of shots) {{
    const cell = document.createElement("div");
    cell.className = "cell";
    const caption = document.createElement("div");
    caption.className = "caption";
    caption.textContent = s.caption;
    cell.appendChild(caption);
    const svg = document.getElementById("watch").content.firstElementChild.cloneNode(true);
    svg.setAttribute("viewBox", "{viewbox}");
    svg.setAttribute("width", "{cell_w}");
    svg.setAttribute("height", "{lcd_h}");
    for (const lcd of ["classic", "custom"]) {{
      svg.querySelector("#" + lcd).style.display = lcd === s.lcd ? "" : "none";
    }}
    const glass = svg.querySelector("#" + s.lcd);
    const ghost = glass.cloneNode(true);
    ghost.removeAttribute("id");
    ghost.style.opacity = {ghost};
    glass.before(ghost);
    const lit = new Set(s.lit);
    glass.querySelectorAll("[data-com][data-seg]").forEach((e) => {{
      e.style.opacity = lit.has(e.dataset.com + ":" + e.dataset.seg) ? 1 : 0;
    }});
    cell.appendChild(svg);
    document.body.appendChild(cell);
  }}
</script>
"""


def fail(message):
    print(f"lcd-shots: {message}", file=sys.stderr)
    sys.exit(1)


def find_chrome():
    for name in (os.environ.get("CHROME"), "google-chrome", "google-chrome-stable", "chromium", "chromium-browser"):
        if name and shutil.which(name):
            return shutil.which(name)
    fail("no Chrome or Chromium on PATH (set CHROME to the binary)")


def run(command, what, timeout):
    """Runs a command; a failure or a hang ends the capture."""
    try:
        done = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        fail(f"{what} did not finish in {timeout} s")
    if done.returncode != 0:
        fail(f"{what} failed (exit {done.returncode}):\n{done.stdout[-2000:]}{done.stderr[-2000:]}")
    return done.stdout


def run_shots(root, face):
    """Builds and runs shot_<face>; returns {lcd: [(name, [com:seg, ...]), ...]}."""
    test = root / "test"
    run(["make", "-s", "-C", str(test), f"build/shot_{face}"], f"building shot_{face}", 600)
    output = run([str(test / "build" / f"shot_{face}")], f"shot_{face}", 300)

    shots = {lcd: [] for lcd in LCDS}
    for line in output.splitlines():
        parts = line.split("\t")
        if len(parts) != 3 or parts[0] not in LCDS:
            fail(f"shot_{face}: unexpected output line: {line!r}")
        lcd, name, pixels = parts
        if not NAME.match(name):
            fail(f"shot_{face}: shot name {name!r} must match {NAME.pattern}")
        if any(name == seen for seen, _ in shots[lcd]):
            fail(f"shot_{face}: two {lcd} shots are named {name!r}")
        shots[lcd].append((name, pixels.split()))
    if not shots["classic"]:
        fail(f"shot_{face} reported no shots")
    # A face is always captured on both LCD types, so neither goes unreviewed.
    if [name for name, _ in shots["classic"]] != [name for name, _ in shots["custom"]]:
        fail(f"shot_{face} must report the same shots, in the same order, for classic and custom")
    return shots


def render(chrome, svg, viewbox, lcd_h, face, lcd, shots, out, tmp):
    """One Chrome run draws every shot of a face and LCD type; Pillow cuts the sheet up."""
    cell_h = CAPTION_H + lcd_h
    rows = -(-len(shots) // COLUMNS)
    page_w, page_h = CELL_W * COLUMNS, cell_h * rows
    page = tmp / f"{face}-{lcd}.html"
    page.write_text(
        PAGE.format(
            svg=svg,
            shots=json.dumps([{"caption": f"{face} / {lcd} / {name}", "lcd": lcd, "lit": lit} for name, lit in shots]),
            viewbox=viewbox,
            page_w=page_w,
            cell_w=CELL_W,
            cell_h=cell_h,
            caption_h=CAPTION_H,
            lcd_h=lcd_h,
            ghost=GHOST_OPACITY,
        ),
        encoding="utf-8",
    )
    sheet = tmp / f"{face}-{lcd}.png"
    run(
        [
            chrome,
            "--headless=new",
            "--disable-gpu",
            "--hide-scrollbars",
            "--force-device-scale-factor=1",
            f"--user-data-dir={tmp / 'chrome-profile'}",
            f"--window-size={page_w},{page_h}",
            f"--screenshot={sheet}",
            page.as_uri(),
        ],
        f"Chrome rendering {face}/{lcd}",
        120,
    )
    if not sheet.exists():
        fail(f"Chrome wrote no screenshot for {face}/{lcd}")

    from PIL import Image  # here, so a checkout with nothing to capture needs no Pillow

    image = Image.open(sheet).convert("RGB")
    if image.size != (page_w, page_h):
        fail(f"Chrome rendered {face}/{lcd} at {image.size}, expected {(page_w, page_h)}")
    directory = out / face / lcd
    directory.mkdir(parents=True)
    for i, (name, _) in enumerate(shots):
        left, top = (i % COLUMNS) * CELL_W, (i // COLUMNS) * cell_h
        # The glass and its ghost segments are at least two colours even with
        # nothing lit; one flat colour means the page's script didn't run.
        glass = image.crop((left, top + CAPTION_H, left + CELL_W, top + cell_h))
        if len(glass.getcolors(CELL_W * lcd_h) or []) < 2:
            fail(f"Chrome drew no LCD for {face}/{lcd}/{name}")
        image.crop((left, top, left + CELL_W, top + cell_h)).save(directory / f"{name}.png")
    return len(shots)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", required=True, type=Path, help="the checkout to capture")
    parser.add_argument("--out", required=True, type=Path, help="directory to create and write PNGs under")
    parser.add_argument("faces", nargs="*", help="faces to capture (default: every test/shots/shot_<face>.c)")
    args = parser.parse_args()

    root, out = args.root.resolve(), args.out.resolve()
    # A checkout from before the shots existed is fine; a path that isn't a
    # checkout of this repo is a mistake, not an empty capture.
    if not (root / "watch-faces").is_dir():
        fail(f"{root} is not a checkout of this repo")
    if out.exists():
        fail(f"{out} already exists")
    for face in args.faces:
        if not NAME.match(face):
            fail(f"face name {face!r} must match {NAME.pattern}")
    out.mkdir(parents=True)

    available = sorted(p.stem[len("shot_"):] for p in (root / "test" / "shots").glob("shot_*.c"))
    faces = args.faces or available
    skipped = [f for f in faces if f not in available]
    faces = [f for f in faces if f in available]
    for face in skipped:
        print(f"lcd-shots: skipped {face}: no test/shots/shot_{face}.c in {root}", file=sys.stderr)
    note = f" (skipped: {', '.join(skipped)})" if skipped else ""
    if not faces:
        print(f"lcd-shots: nothing to capture in {root}{note}")
        return

    chrome = find_chrome()
    shell = (root / "watch-library" / "simulator" / "shell.html").read_text(encoding="utf-8")
    svg = shell[shell.index("<svg") : shell.index("</svg>") + len("</svg>")]
    glass = GLASS.search(svg)
    if not glass:
        fail("could not find the LCD glass in watch-library/simulator/shell.html")
    x, y, w, h = (float(v) for v in glass.groups())
    lcd_h = round(CELL_W * h / w)

    total = 0
    with tempfile.TemporaryDirectory(prefix="lcd-shots.") as tmp:
        for face in faces:
            for lcd, shots in run_shots(root, face).items():
                total += render(chrome, svg, f"{x} {y} {w} {h}", lcd_h, face, lcd, shots, out, Path(tmp))
    print(f"lcd-shots: {total} captures of {len(faces)} face(s) in {out}{note}")


if __name__ == "__main__":
    main()
