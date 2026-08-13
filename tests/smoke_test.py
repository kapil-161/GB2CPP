#!/usr/bin/env python3
"""
GB2 headless regression smoke-test.

Runs GB2 in headless mode on a handful of known cases and checks the results
against saved baselines, so an accidental change to plotting/metrics is caught
automatically instead of by eyeballing plots.

Two kinds of check:
  * metrics : compares the fit-metrics CSV (RMSE/d-stat per treatment) exactly
              against tests/baselines/<case>.csv — this is where subtle bugs
              hide (e.g. metrics attributed to the wrong experiment).
  * render  : just confirms a non-trivial PNG was produced (a plain "did it
              crash / produce output" smoke check).

Usage:
    python smoke_test.py            run the tests, compare to baselines
    python smoke_test.py --update   (re)create the baselines from current output
                                     — do this only when the output *should* change

No third-party dependencies. Needs a built GB2.exe (build_win/bin or
manual_deployment, or point GB2_BINARY at one) and the DSSAT data under
GB2_DSSAT_BASE (default C:/DSSAT48 on Windows, /Applications/DSSAT48 on macOS).
"""

from __future__ import annotations
import argparse
import difflib
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BASELINE_DIR = HERE / "baselines"

IS_WIN = platform.system() == "Windows"
DSSAT_BASE = os.environ.get("GB2_DSSAT_BASE", "C:/DSSAT48" if IS_WIN else "/Applications/DSSAT48")
WHEAT = f"{DSSAT_BASE}/Wheat"
SOYBEAN = f"{DSSAT_BASE}/Soybean"

# Qt versions build_and_deploy.bat auto-detects — keep roughly in sync.
WINDOWS_QT_VERSIONS = ["6.9.1", "6.8.2", "6.8.1", "6.11.1", "6.11.0", "6.7.1", "6.6.1"]


# --- test cases --------------------------------------------------------------
# Each case runs: GB2 <args> --save <png> [--metrics <csv>]
# "metrics": True  → compare the CSV to a baseline (the real regression guard)
# "metrics": False → just require the PNG to be produced (render smoke check)
CASES = [
    {
        "name": "wheat_timeseries",
        "args": [DSSAT_BASE, WHEAT, "PlantGro.OUT", "--xvar", "DAP", "--yvar", "LAID,CWAD,GWAD"],
        "metrics": True,
    },
    {
        # Guards the disease/pest-module fix: DISMO.OUT writes EXPERIMENT with a
        # ".SBX" extension; if the strip regresses, observed SEV% stops matching
        # and this baseline (n>0, real RMSE) goes empty → test fails.
        "name": "soybean_dismo_sev",
        "args": [DSSAT_BASE, SOYBEAN, "DISMO.OUT", "--xvar", "DATE", "--yvar", "SEV%"],
        "metrics": True,
    },
    {
        "name": "wheat_scatter",
        "args": ["Wheat", "--scatter", "--scatter-metrics", "RMSE,R2"],
        "metrics": False,
    },
    {
        "name": "wheat_experiment_grid",
        "args": [DSSAT_BASE, WHEAT, "PlantGro.OUT", "--xvar", "DAP", "--yvar", "LAID,CWAD", "--grid"],
        "metrics": False,
    },
]


def find_gb2() -> Path:
    override = os.environ.get("GB2_BINARY")
    if override and Path(override).is_file():
        return Path(override)
    candidates = [REPO / "build_win" / "bin" / "GB2.exe",
                  REPO / "manual_deployment" / "GB2.exe"]
    for c in candidates:
        if c.is_file():
            return c
    sys.exit("ERROR: GB2.exe not found. Build it (build_win/bin/GB2.exe) or set GB2_BINARY.")


def headless_env() -> dict:
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    if IS_WIN:
        # Put a Qt mingw_64 bin dir on PATH so Qt finds its DLLs + offscreen plugin.
        qt_bin = os.environ.get("GB2_QT_BIN_DIR")
        if not qt_bin:
            for v in WINDOWS_QT_VERSIONS:
                cand = Path(f"C:/Qt/{v}/mingw_64/bin")
                if cand.is_dir():
                    qt_bin = str(cand)
                    break
        if qt_bin:
            env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")
        env.setdefault("QT_QPA_FONTDIR", r"C:\Windows\Fonts")
        env.setdefault("QTFRAMEWORK_BYPASS_LICENSE_CHECK", "1")
    return env


def normalize_csv(text: str) -> str:
    # Compare content, ignore trailing whitespace / line-ending differences.
    return "\n".join(line.rstrip() for line in text.strip().splitlines())


def run_case(gb2: Path, env: dict, case: dict, tmp: Path):
    png = tmp / f"{case['name']}.png"
    args = [str(gb2), *case["args"], "--save", str(png)]
    csv = None
    if case["metrics"]:
        csv = tmp / f"{case['name']}.csv"
        args += ["--metrics", str(csv)]
    proc = subprocess.run(args, env=env, capture_output=True, text=True, timeout=60)
    return proc, png, csv


def main() -> int:
    ap = argparse.ArgumentParser(description="GB2 headless regression smoke-test")
    ap.add_argument("--update", action="store_true", help="(re)write baselines from current output")
    args = ap.parse_args()

    gb2 = find_gb2()
    env = headless_env()
    BASELINE_DIR.mkdir(exist_ok=True)

    print(f"GB2:   {gb2}")
    print(f"DSSAT: {DSSAT_BASE}")
    print(f"Mode:  {'UPDATE BASELINES' if args.update else 'CHECK'}\n")

    passed = failed = 0
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for case in CASES:
            name = case["name"]
            try:
                proc, png, csv = run_case(gb2, env, case, tmp)
            except subprocess.TimeoutExpired:
                print(f"[FAIL] {name}: timed out")
                failed += 1
                continue

            if case["metrics"]:
                if not csv or not csv.is_file():
                    print(f"[FAIL] {name}: no metrics CSV produced (exit {proc.returncode})")
                    if proc.stderr.strip():
                        print("       stderr:", proc.stderr.strip()[:300])
                    failed += 1
                    continue
                got = normalize_csv(csv.read_text(encoding="utf-8", errors="replace"))
                baseline = BASELINE_DIR / f"{name}.csv"
                if args.update:
                    baseline.write_text(got + "\n", encoding="utf-8")
                    print(f"[SAVE] {name}: baseline written ({len(got.splitlines())} rows)")
                    passed += 1
                    continue
                if not baseline.is_file():
                    print(f"[FAIL] {name}: no baseline yet — run with --update to create it")
                    failed += 1
                    continue
                want = normalize_csv(baseline.read_text(encoding="utf-8", errors="replace"))
                if got == want:
                    print(f"[PASS] {name}: metrics match baseline")
                    passed += 1
                else:
                    print(f"[FAIL] {name}: metrics changed vs baseline")
                    diff = difflib.unified_diff(want.splitlines(), got.splitlines(),
                                                fromfile="baseline", tofile="current", lineterm="")
                    for line in diff:
                        print("   ", line)
                    failed += 1
            else:
                ok = png.is_file() and png.stat().st_size > 5000
                if args.update:
                    print(f"[SAVE] {name}: render check (no baseline needed)")
                    passed += 1
                elif ok:
                    print(f"[PASS] {name}: produced {png.stat().st_size} byte PNG")
                    passed += 1
                else:
                    print(f"[FAIL] {name}: no/'too small' PNG (exit {proc.returncode})")
                    if proc.stderr.strip():
                        print("       stderr:", proc.stderr.strip()[:300])
                    failed += 1

    print(f"\n{'='*48}\n{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
