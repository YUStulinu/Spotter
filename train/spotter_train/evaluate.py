"""The coach end to end on MM-Fit's test workouts, against the dataset's labels.

    python -m spotter_train.evaluate [--source mmfit|video]

For every test workout, the whole session goes through `spotter-cli run`: skeletons, the
classifier every few frames, sets starting and ending on their own, repetitions counted. Each
labelled set is matched to the predicted set that overlaps it most. Reported:

- sets found (a predicted set overlaps the labelled one) and sets invented (overlapping none);
- the exercise of each found set;
- repetitions per found set: mean absolute error, exactly right, within one;
- the counter alone, given the true exercise and the labelled set with a second either side (`spotter-cli count`),
  to separate counting errors from recognition errors.

--source mmfit uses the dataset's skeletons (OpenPose); --source video Spotter's own pose model run
on the videos (scripts/fetch-videos.sh --videos, then spotter-cli pose), which is what the app sees.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from . import mmfit
from .tune_reps import keypoint_file

ROOT = Path(__file__).resolve().parents[2]
PAD = 1.0  # seconds around a labelled set given to the counter alone


def run(cli: str, kp: Path) -> list[dict]:
    out = subprocess.run([cli, "run", str(kp)], check=True, capture_output=True, text=True).stdout
    return [json.loads(line) for line in out.splitlines() if line.startswith("{")]


def predicted_sets(events: list[dict]) -> list[dict]:
    sets, open_ = [], None
    for e in events:
        if e["event"] == "set_started":
            open_ = {"exercise": e["exercise"], "start": e["t"]}
        elif e["event"] == "set_ended" and open_:
            if e["reps"] > 0:  # a set with no repetitions (dropped by the session) is not a set
                open_.update(end=e["t"], reps=e["reps"])
                sets.append(open_)
            open_ = None
    return sets


def counter_only(cli: str, kp: Path, s: mmfit.Set, offset: float) -> int:
    # A second either side: the labels start at the first movement, and the counter needs to see
    # where the lifter starts from (as it does live).
    out = subprocess.run([cli, "count", str(kp), s.exercise, f"{s.start / mmfit.FPS - offset - PAD:.3f}", f"{s.end / mmfit.FPS - offset + PAD:.3f}"],
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)["count"]


def evaluate(cli: str, ids: list[str], source: str) -> dict:
    rows = []
    invented = 0
    for wid in ids:
        if source == "mmfit":
            kp, offset = keypoint_file(wid, ROOT / "data" / "kp"), 0.0
        else:
            kp, offset = ROOT / "data" / "kp" / f"{wid}.kp", 0.0  # times are video frame / fps, as the labels
        labels = mmfit.read_sets(mmfit.DATA / wid / f"{wid}_labels.csv")
        pred = predicted_sets(run(cli, kp))
        used = set()
        for s in labels:
            a, b = s.start / mmfit.FPS - offset, s.end / mmfit.FPS - offset
            best, overlap = None, 0.0
            for i, p in enumerate(pred):
                o = min(b, p["end"]) - max(a, p["start"])
                if o > overlap:
                    best, overlap = i, o
            row = {"workout": wid, "exercise": s.exercise, "reps": s.reps, "counter_only": counter_only(cli, kp, s, offset)}
            if best is not None:
                used.add(best)
                row.update(found=True, predicted=pred[best]["exercise"], predicted_reps=pred[best]["reps"])
            else:
                row.update(found=False)
            rows.append(row)
        invented += len([i for i in range(len(pred)) if i not in used])
        print(f"{wid}: {len(labels)} sets labelled, {len(pred)} predicted", flush=True)

    found = [r for r in rows if r["found"]]
    right = [r for r in found if r["predicted"] == r["exercise"]]
    err = np.array([abs(r["predicted_reps"] - r["reps"]) for r in right])
    alone = np.array([abs(r["counter_only"] - r["reps"]) for r in rows])
    per_ex = {}
    for ex in mmfit.EXERCISES:
        mine = [r for r in rows if r["exercise"] == ex]
        if not mine:
            continue
        e = np.array([abs(r["counter_only"] - r["reps"]) for r in mine])
        ok = [r for r in mine if r["found"] and r["predicted"] == ex]
        per_ex[ex] = {"sets": len(mine), "recognised": len(ok) / len(mine), "counter_mae": float(e.mean()),
                      "counter_exact": float((e == 0).mean()), "counter_within_1": float((e <= 1).mean())}
    return {
        "sets": len(rows), "found": len(found) / len(rows), "invented": invented,
        "exercise_right": len(right) / max(1, len(found)),
        "reps_mae": float(err.mean()) if len(err) else None, "reps_exact": float((err == 0).mean()) if len(err) else None,
        "reps_within_1": float((err <= 1).mean()) if len(err) else None,
        "counter_only_mae": float(alone.mean()), "counter_only_exact": float((alone == 0).mean()),
        "counter_only_within_1": float((alone <= 1).mean()),
        "per_exercise": per_ex, "rows": rows,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=str(ROOT / "build" / "Release" / "spotter-cli.exe"))
    ap.add_argument("--source", choices=["mmfit", "video"], default="mmfit")
    ap.add_argument("--workouts", nargs="*")
    args = ap.parse_args()
    results = {}
    splits = {"test_unseen": mmfit.TEST_UNSEEN, "test_seen": mmfit.TEST_SEEN} if args.source == "mmfit" else \
        {"test_unseen_video": args.workouts or [w for w in mmfit.TEST_UNSEEN if (ROOT / "data" / "kp" / f"{w}.kp").exists()]}
    for name, ids in splits.items():
        r = evaluate(args.cli, ids, args.source)
        results[name] = r
        print(f"{name}: sets found {r['found']:.2f}, invented {r['invented']}, exercise right {r['exercise_right']:.2f}, "
              f"reps MAE {r['reps_mae']:.2f} exact {r['reps_exact']:.2f} within 1 {r['reps_within_1']:.2f}; "
              f"counter alone MAE {r['counter_only_mae']:.2f} exact {r['counter_only_exact']:.2f}", flush=True)
    out = ROOT / "results" / f"coach_{args.source}.json"
    out.write_text(json.dumps(results, indent=1))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
