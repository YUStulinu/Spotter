"""The form checks on real repetitions: MM-Fit's test workouts as they are, and the same
repetitions with a fault put in.

    python -m spotter_train.form_eval

MM-Fit has no form labels, so two things are measured:
- how often each check fires on the repetitions as filmed (people in MM-Fit move reasonably; a
  check that fires on most of them is too strict);
- how often it fires when a fault of a known size is added to the same repetitions: knees pulled
  towards each other at the bottom of a squat, the upper body swung during a curl, one hand left
  lower at the top of a press. The fault grows with the repetition's depth, as the real one does.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np

from . import mmfit

ROOT = Path(__file__).resolve().parents[2]
L_SH, R_SH, L_EL, R_EL, L_WR, R_WR, L_HIP, R_HIP, L_KN, R_KN, L_AN, R_AN = 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
UPPER = [0, 1, 2, 3, 4, L_SH, R_SH, L_EL, R_EL, L_WR, R_WR]


def depth(k: np.ndarray, joints: list[int], invert: bool = False) -> np.ndarray:
    """How far into the movement each frame is, 0..1, from the mean height of some joints."""
    y = k[:, joints, 1].mean(1)
    lo, hi = np.percentile(y, 5), np.percentile(y, 95)
    d = np.clip((y - lo) / max(1e-6, hi - lo), 0, 1)
    return 1 - d if invert else d


def torso(k: np.ndarray) -> float:
    return float(np.median(np.linalg.norm((k[:, L_SH, :2] + k[:, R_SH, :2]) / 2 - (k[:, L_HIP, :2] + k[:, R_HIP, :2]) / 2, axis=1)))


def knees_in(k: np.ndarray, size: float) -> np.ndarray:
    """At the bottom of the squat the knees are `size` of the way to the middle of the ankles."""
    k = k.copy()
    d = depth(k, [L_HIP, R_HIP])[:, None]  # image y grows downwards: low hips = large y = deep
    centre = (k[:, L_AN, 0] + k[:, R_AN, 0])[:, None] / 2
    for j in (L_KN, R_KN):
        k[:, j, 0] = centre[:, 0] + (k[:, j, 0] - centre[:, 0]) * (1 - size * d[:, 0])
    return k


def swing(k: np.ndarray, degrees: float) -> np.ndarray:
    """The upper body leans back by up to `degrees` as the weight comes up (around the hips)."""
    k = k.copy()
    d = depth(k, [L_WR, R_WR], invert=True)  # wrists high = the top of the curl
    hips = (k[:, L_HIP, :2] + k[:, R_HIP, :2]) / 2
    for i in range(len(k)):
        a = np.deg2rad(degrees * d[i])
        rot = np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]])
        k[i, UPPER, :2] = (k[i, UPPER, :2] - hips[i]) @ rot.T + hips[i]
    return k


def uneven(k: np.ndarray, share: float) -> np.ndarray:
    """At the top of the press the right hand stays `share` torso lengths lower."""
    k = k.copy()
    d = depth(k, [L_WR, R_WR], invert=True)
    t = torso(k)
    for j in (R_WR, R_EL):
        k[:, j, 1] += share * t * d * (1.0 if j == R_WR else 0.5)
    return k


CASES = {  # exercise: (fault id, injection, sizes)
    "squats": ("knees_in", knees_in, [0.3, 0.5, 0.7]),
    "bicep_curls": ("swing", swing, [10, 20, 30]),
    "dumbbell_shoulder_press": ("uneven", uneven, [0.2, 0.35, 0.5]),
}


def count(cli: str, kp: Path, exercise: str, a: float, b: float) -> dict:
    out = subprocess.run([cli, "count", str(kp), exercise, f"{a:.3f}", f"{b:.3f}"], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def main():
    cli = str(ROOT / "build" / "Release" / "spotter-cli.exe")
    results = {}
    clean_flags: dict[str, dict[str, int]] = {}
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        for exercise, (fault, inject, sizes) in CASES.items():
            row = {"fault": fault, "clean": {"reps": 0, "flagged": 0}, "injected": {str(s): {"reps": 0, "flagged": 0} for s in sizes}}
            for wid in mmfit.TEST_UNSEEN + mmfit.TEST_SEEN:
                w = mmfit.load(wid)
                for s in w.sets:
                    if s.exercise != exercise:
                        continue
                    m = (w.frames >= s.start - 15) & (w.frames <= s.end + 15)
                    times, k = w.frames[m] / mmfit.FPS, w.keypoints[m]
                    a, b = times[0], times[-1]
                    variants = [("clean", k)] + [(str(z), inject(k, z)) for z in sizes]
                    for name, kk in variants:
                        path = tmp / "set.kp"
                        mmfit.write_keypoints(path, times, kk)
                        r = count(cli, path, exercise, a, b)
                        flagged = len({round(f["t"], 1) for f in r["faults"] if f["id"] == fault})
                        target = row["clean"] if name == "clean" else row["injected"][name]
                        target["reps"] += r["count"]
                        target["flagged"] += min(flagged, r["count"])
                        if name == "clean":
                            for f in r["faults"]:
                                clean_flags.setdefault(exercise, {}).setdefault(f["id"], 0)
                                clean_flags[exercise][f["id"]] += 1
            for v in [row["clean"], *row["injected"].values()]:
                v["rate"] = v["flagged"] / max(1, v["reps"])
            results[exercise] = row
            print(exercise, fault, "clean", f"{row['clean']['rate']:.2f}",
                  " ".join(f"{z}:{v['rate']:.2f}" for z, v in row["injected"].items()), flush=True)
    results["all_checks_on_clean_reps"] = clean_flags
    (ROOT / "results" / "form.json").write_text(json.dumps(results, indent=1))
    print("wrote results/form.json")


if __name__ == "__main__":
    main()
