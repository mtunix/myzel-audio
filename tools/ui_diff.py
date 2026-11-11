#!/usr/bin/env python3
"""Renders a plugin's UI snapshots and compares them pixel by pixel with a baseline.

Usage:
    tools/ui_diff.py <plugin> <build-dir> <baseline-dir> --record   # save the baseline
    tools/ui_diff.py <plugin> <build-dir> <baseline-dir>            # compare against it

Needs xvfb-run and Pillow. Exit code 1 when any image differs, is missing or is new.
Difference images (changed pixels in red) go to <baseline-dir>/../<plugin>-diff/.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops


def render(plugin: str, build: Path, out: Path) -> None:
    name = plugin.capitalize()
    tool = build / "plugins" / plugin / "tools" / f"{name}UiSnapshot_artefacts" / "Release" / f"{name}UiSnapshot"
    if not tool.exists():
        sys.exit(f"{tool} not found: build the {name}UiSnapshot target first")
    # The virtual screen must be larger than the editor, or JUCE shrinks the window.
    subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 3000x2000x24", str(tool), str(out)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def compare(baseline: Path, current: Path, diff_dir: Path) -> bool:
    ok = True
    names = sorted({p.name for p in baseline.glob("*.png")} | {p.name for p in current.glob("*.png")})
    for name in names:
        a, b = baseline / name, current / name
        if not a.exists() or not b.exists():
            print(f"  {'new' if not a.exists() else 'missing':>9}  {name}")
            ok = False
            continue
        ia, ib = Image.open(a).convert("RGBA"), Image.open(b).convert("RGBA")
        if ia.size != ib.size:
            print(f"  {'size':>9}  {name}: {ia.size} -> {ib.size}")
            ok = False
            continue
        delta = ImageChops.difference(ia, ib)
        if delta.getbbox(alpha_only=False) is None:
            continue
        r, g, b, alpha = delta.split()
        mask = ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(b, alpha)).point(lambda v: 255 if v else 0)
        changed = sum(1 for v in mask.getdata() if v)
        worst = max(hi for _, hi in delta.getextrema())
        print(f"  {changed:>9}  {name}: pixels changed, max channel delta {worst}")
        diff_dir.mkdir(parents=True, exist_ok=True)
        overlay = ib.convert("RGB")
        overlay.paste((255, 0, 0), mask=mask)
        overlay.save(diff_dir / name)
        ok = False
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugin")
    parser.add_argument("build", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--record", action="store_true", help="save the baseline instead of comparing")
    args = parser.parse_args()

    if args.record:
        shutil.rmtree(args.baseline, ignore_errors=True)
        args.baseline.mkdir(parents=True)
        render(args.plugin, args.build, args.baseline)
        print(f"{args.plugin}: baseline of {len(list(args.baseline.glob('*.png')))} images in {args.baseline}")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        current = Path(tmp)
        render(args.plugin, args.build, current)
        diff_dir = args.baseline.parent / f"{args.plugin}-diff"
        shutil.rmtree(diff_dir, ignore_errors=True)
        if compare(args.baseline, current, diff_dir):
            print(f"{args.plugin}: all {len(list(current.glob('*.png')))} images identical")
            return 0
        print(f"{args.plugin}: differences, see {diff_dir}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
