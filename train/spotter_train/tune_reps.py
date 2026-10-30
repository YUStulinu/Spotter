"""Chooses the repetition counter's signal, direction and range for each exercise on MM-Fit's
training workouts, checks the choice on the validation workouts, and writes core/exercises.inc.

    python -m spotter_train.tune_reps [--cli ../build/Release/spotter-cli.exe]

Every candidate signal (hip height, knee and elbow angles, ...) is tried in both directions with a
range taken from the training sets themselves (0.6, 0.8 or 1.0 times the median spread of that
signal over the exercise's sets); the C++ counter counts with each (spotter-cli count-grid), and the
candidate with the smallest mean absolute error in repetitions per set wins.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path

import numpy as np

from . import mmfit

ROOT = Path(__file__).resolve().parents[2]
SIGNALS = ["hip_height", "knee_angle", "elbow_angle", "hip_angle", "wrist_lift", "arm_raise", "stance", "shoulder_height", "nose_height"]
NAMES = {"squats": "Squat", "lunges": "Lunge", "pushups": "Push-up", "situps": "Sit-up", "jumping_jacks": "Jumping jack",
         "bicep_curls": "Biceps curl", "tricep_extensions": "Triceps extension", "dumbbell_rows": "Dumbbell row",
         "dumbbell_shoulder_press": "Shoulder press", "lateral_shoulder_raises": "Lateral raise"}
ENUM = {"hip_height": "HipHeight", "knee_angle": "KneeAngle", "elbow_angle": "ElbowAngle", "hip_angle": "HipAngle",
        "wrist_lift": "WristLift", "arm_raise": "ArmRaise", "stance": "Stance", "shoulder_height": "ShoulderHeight",
        "nose_height": "NoseHeight"}
MIN_REP = {"jumping_jacks": 0.4}
SCALES = (0.6, 0.8, 1.0)


def keypoint_file(wid: str, folder: Path) -> Path:
    """MM-Fit's skeletons in the shared keypoint format, the time being frame / 30."""
    path = folder / f"mmfit-{wid}.kp"
    if not path.exists():
        w = mmfit.load(wid)
        mmfit.write_keypoints(path, w.frames / mmfit.FPS, w.keypoints)
    return path


def signals(cli: str, kp: Path, out: Path) -> dict[str, np.ndarray]:
    subprocess.run([cli, "signals", str(kp), str(out)], check=True)
    with open(out, newline="") as f:
        rows = list(csv.DictReader(f))
    cols = {"t": np.array([float(r["t"]) for r in rows])}
    for s in SIGNALS:
        cols[s] = np.array([float(r[s]) if r[s] else np.nan for r in rows])
    return cols


def spread(values: np.ndarray) -> float:
    v = values[~np.isnan(values)]
    return float(np.percentile(v, 90) - np.percentile(v, 10)) if len(v) > 10 else 0.0


def count_grid(cli: str, kp: Path, sets: list[mmfit.Set], grid: list[tuple], tmp: Path) -> np.ndarray:
    pad = 1.0  # a second either side, as the evaluation gives the counter alone
    (tmp / "sets.csv").write_text("".join(f"{s.start / mmfit.FPS - pad:.3f},{s.end / mmfit.FPS + pad:.3f}\n" for s in sets))
    (tmp / "grid.csv").write_text("".join(f"{g[0]},{g[1]},{g[2]:.4f},0,{g[3]}\n" for g in grid))
    out = subprocess.run([cli, "count-grid", str(kp), str(tmp / "sets.csv"), str(tmp / "grid.csv")],
                         check=True, capture_output=True, text=True).stdout
    counts = np.zeros((len(sets), len(grid)), dtype=int)
    for line in out.split():
        s, g, c = map(int, line.split(","))
        counts[s, g] = c
    return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=str(ROOT / "build" / "Release" / "spotter-cli.exe"))
    args = ap.parse_args()
    folder = ROOT / "data" / "kp"
    folder.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        # The spread of every signal over every training set.
        spreads: dict[tuple[str, str], list[float]] = defaultdict(list)
        workouts = {}
        for wid in mmfit.TRAIN + mmfit.VAL:
            kp = keypoint_file(wid, folder)
            sets = mmfit.read_sets(mmfit.DATA / wid / f"{wid}_labels.csv")
            workouts[wid] = (kp, sets)
            if wid in mmfit.TRAIN:
                sig = signals(args.cli, kp, tmp / "signals.csv")
                for s in sets:
                    inside = (sig["t"] >= s.start / mmfit.FPS) & (sig["t"] <= s.end / mmfit.FPS)
                    for name in SIGNALS:
                        spreads[(s.exercise, name)].append(spread(sig[name][inside]))
        grid, owner = [], []
        for ex in mmfit.EXERCISES:
            for name in SIGNALS:
                r = float(np.median(spreads[(ex, name)]))
                if r <= 0:
                    continue
                for direction in (1, -1):
                    for scale in SCALES:
                        grid.append((name, direction, r * scale, MIN_REP.get(ex, 0.6)))
                        owner.append(ex)
        owner = np.array(owner)
        errors = {"train": defaultdict(list), "val": defaultdict(list)}
        for wid, (kp, sets) in workouts.items():
            counts = count_grid(args.cli, kp, sets, grid, tmp)
            split = "train" if wid in mmfit.TRAIN else "val"
            for i, s in enumerate(sets):
                errors[split][s.exercise].append(np.abs(counts[i] - s.reps))
            print(f"{wid}: {len(sets)} sets", flush=True)

    chosen, report = {}, {}
    for ex in mmfit.EXERCISES:
        mine = np.where(owner == ex)[0]
        train = np.array(errors["train"][ex])[:, mine]  # (sets, candidates)
        score = train.mean(0) - 0.01 * (train == 0).mean(0)
        best = mine[int(np.argmin(score))]
        chosen[ex] = grid[best]
        tr = np.array(errors["train"][ex])[:, best]
        va = np.array(errors["val"][ex])[:, best] if errors["val"][ex] else np.array([])
        report[ex] = {"signal": grid[best][0], "direction": grid[best][1], "amplitude": grid[best][2],
                      "train_mae": float(tr.mean()), "train_exact": float((tr == 0).mean()), "train_sets": len(tr),
                      "val_mae": float(va.mean()) if len(va) else None, "val_exact": float((va == 0).mean()) if len(va) else None,
                      "val_within_1": float((va <= 1).mean()) if len(va) else None, "val_sets": len(va)}
        print(f"{ex:25s} {grid[best][0]:16s} {grid[best][1]:+d} {grid[best][2]:8.3f}  train MAE {tr.mean():.2f}  "
              f"val MAE {report[ex]['val_mae'] if va.size else float('nan'):.2f} exact {report[ex]['val_exact'] or 0:.2f}")

    lines = ["// id, name, signal, direction, amplitude, count at the far end, shortest repetition (s).",
             "// Written by train/spotter_train/tune_reps.py from MM-Fit's training workouts; the deadlift,",
             "// which MM-Fit does not have, is set by hand."]
    for ex in mmfit.EXERCISES:
        name, direction, amp, min_rep = chosen[ex]
        lines.append(f'{{"{ex}", "{NAMES[ex]}", Signal::{ENUM[name]}, {direction}, {amp:.4g}f, false, {min_rep}}},')
    lines.append('{"deadlift", "Deadlift", Signal::HipAngle, 1, 60.f, true, 1.0},')
    (ROOT / "core" / "exercises.inc").write_text("\n".join(lines) + "\n")
    (ROOT / "results").mkdir(exist_ok=True)
    (ROOT / "results" / "reps_tuning.json").write_text(json.dumps(report, indent=1))
    print("wrote core/exercises.inc")


if __name__ == "__main__":
    main()
