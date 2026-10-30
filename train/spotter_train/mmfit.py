"""MM-Fit (Strömbäck, Huang and Radu, IMWUT 2020; CC BY 4.0): 21 workouts of 10 exercises, filmed
from the front, with the start and end frame and the repetitions of every set.

The dataset's 2D poses come from OpenPose (18 keypoints, with a neck); Spotter's pose model gives the
17 COCO keypoints, so the neck is dropped and the rest reordered. Missing keypoints are (0, 0) in
the dataset and get a score of 0 here.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "data" / "mmfit" / "mm-fit"

# The model's classes: the ten exercises and everything else (rest, setting up, walking about).
EXERCISES = ["squats", "lunges", "pushups", "situps", "jumping_jacks", "bicep_curls", "tricep_extensions",
             "dumbbell_rows", "dumbbell_shoulder_press", "lateral_shoulder_raises"]
CLASSES = EXERCISES + ["none"]
NONE = len(EXERCISES)

# The dataset's split (github.com/KDMStromback/mm-fit, train_multimodal_ar.py).
TRAIN = ["w01", "w02", "w03", "w04", "w06", "w07", "w08", "w16", "w17", "w18"]
VAL = ["w14", "w15", "w19"]
TEST_SEEN = ["w09", "w10", "w11"]           # people also in the training workouts
TEST_UNSEEN = ["w00", "w05", "w12", "w13", "w20"]  # people never seen in training

# OpenPose COCO-18: nose, neck, r-shoulder, r-elbow, r-wrist, l-shoulder, l-elbow, l-wrist, r-hip,
# r-knee, r-ankle, l-hip, l-knee, l-ankle, r-eye, l-eye, r-ear, l-ear. Index of each COCO-17 joint:
OPENPOSE_TO_COCO = [0, 15, 14, 17, 16, 5, 2, 6, 3, 7, 4, 11, 8, 12, 9, 13, 10]

FPS = 30.0


@dataclass
class Set:
    start: int  # first frame
    end: int    # last frame
    reps: int
    exercise: str


@dataclass
class Workout:
    id: str
    frames: np.ndarray     # frame numbers, (n,)
    keypoints: np.ndarray  # (n, 17, 3): x, y, score
    sets: list[Set]

    def labels(self) -> np.ndarray:
        """The class of every frame."""
        y = np.full(len(self.frames), NONE, dtype=np.int64)
        for s in self.sets:
            y[(self.frames >= s.start) & (self.frames <= s.end)] = EXERCISES.index(s.exercise)
        return y


def read_sets(path: Path) -> list[Set]:
    with open(path, newline="") as f:
        return [Set(int(r[0]), int(r[1]), int(r[2]), r[3].strip()) for r in csv.reader(f) if r]


def load(wid: str, root: Path = DATA) -> Workout:
    raw = np.load(root / wid / f"{wid}_pose_2d.npy")  # (2, n, 19): frame number, then 18 keypoints
    frames = raw[0, :, 0].astype(np.int64)
    xy = raw[:, :, 1:].transpose(1, 2, 0)[:, OPENPOSE_TO_COCO]  # (n, 17, 2)
    score = (np.abs(xy).sum(-1) > 0).astype(np.float32)[..., None]
    kp = np.concatenate([xy.astype(np.float32), score], axis=-1)
    return Workout(wid, frames, kp, read_sets(root / wid / f"{wid}_labels.csv"))


def write_keypoints(path: Path, times: np.ndarray, keypoints: np.ndarray):
    """The keypoint file the C++ side reads and writes: a header, then one line per frame: the time in
    seconds and 17 x, y, score triples."""
    with open(path, "w", newline="\n") as f:
        f.write("# spotter keypoints v1: t, then x y score for the 17 COCO joints\n")
        for t, k in zip(times, keypoints):
            f.write(f"{t:.4f} " + " ".join(f"{v:.2f}" if i % 3 < 2 else f"{v:.3f}" for i, v in enumerate(k.reshape(-1))) + "\n")


def read_keypoints(path: Path) -> tuple[np.ndarray, np.ndarray]:
    rows = np.loadtxt(path, comments="#", dtype=np.float32, ndmin=2)
    return rows[:, 0], rows[:, 1:].reshape(-1, 17, 3)
