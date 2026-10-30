"""Trains the exercise classifier on MM-Fit's training workouts and exports it to ONNX.

    python -m spotter_train.train [--epochs 40] [--out ../models/exercise.onnx]

A window is 64 frames (2.1 s at 30 Hz); its label is the class of most of its second half, which is
what the app needs to know at the newest frame. Training windows are augmented so that the model sees
what a different camera and pose model give: mirrored people, rotation, scale, noise, joints the pose
model was unsure of, legs out of the frame, and faster or slower movement.
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
from torch import nn

from . import mmfit
from .model import WINDOW, ExerciseNet

ROOT = Path(__file__).resolve().parents[2]
# COCO joints with their mirror image.
MIRROR = [0, 2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15]


def window_labels(y: np.ndarray, starts: np.ndarray) -> np.ndarray:
    """The class covering most of each window's second half."""
    half = y[starts[:, None] + np.arange(WINDOW // 2, WINDOW)[None]]
    counts = np.stack([(half == c).sum(1) for c in range(len(mmfit.CLASSES))], 1)
    return counts.argmax(1)


class Windows:
    def __init__(self, ids: list[str], stride: int, augment: bool, rng: np.random.Generator):
        self.kp, self.y = [], []
        for w in ids:
            wk = mmfit.load(w)
            self.kp.append(wk.keypoints)
            self.y.append(wk.labels())
        self.augment, self.rng, self.stride = augment, rng, stride
        # Room for slowing down: up to 1.5x the window's frames.
        self.index = [(i, s) for i, k in enumerate(self.kp) for s in range(0, len(k) - int(WINDOW * 1.5), stride)]
        self.labels = np.array([window_labels(self.y[i], np.array([s]))[0] for i, s in self.index])

    def __len__(self):
        return len(self.index)

    def get(self, idx: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        xs, ys = [], []
        for j in idx:
            i, s = self.index[j]
            if self.augment:
                speed = self.rng.uniform(0.7, 1.45)
                t = s + np.arange(WINDOW) * speed
                y = self.y[i][np.round(t[WINDOW // 2 :]).astype(int)]
                label = np.bincount(y, minlength=len(mmfit.CLASSES)).argmax()
                lo = np.floor(t).astype(int)
                f = (t - lo)[:, None, None]
                k = self.kp[i][lo] * (1 - f) + self.kp[i][lo + 1] * f
                xs.append(self.distort(k))
                ys.append(label)
            else:
                xs.append(self.kp[i][s : s + WINDOW])
                ys.append(self.labels[j])
        return np.stack(xs).astype(np.float32), np.array(ys)

    def distort(self, k: np.ndarray) -> np.ndarray:
        r = self.rng
        k = k.copy()
        xy = k[..., :2]
        centre = xy.reshape(-1, 2).mean(0)
        if r.random() < 0.5:  # mirrored person
            xy[..., 0] = 2 * centre[0] - xy[..., 0]
            k = k[:, MIRROR]
            xy = k[..., :2]
        a = np.deg2rad(r.normal(0, 6))
        rot = np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]]) * r.uniform(0.5, 1.8)
        rot[1, 1] *= r.uniform(0.9, 1.1)  # a little perspective
        xy[:] = (xy - centre) @ rot.T + centre + r.normal(0, 80, 2)
        size = np.ptp(xy[..., 1]) + 1
        xy += r.normal(0, r.uniform(0.003, 0.015) * size, xy.shape)  # pose model jitter
        score = k[..., 2]
        score[:] = np.clip(score * r.uniform(0.6, 1.0, score.shape), 0, 1)
        if r.random() < 0.3:  # a few unsure joints
            score[:, r.choice(17, r.integers(1, 4), replace=False)] = r.uniform(0, 0.25)
        if r.random() < 0.1:  # legs out of the frame
            score[:, 13:] = 0
        if r.random() < 0.1:  # a few frames without anyone
            n = r.integers(1, 8)
            at = r.integers(0, WINDOW - n)
            score[at : at + n] = 0
        return k


def evaluate(model: nn.Module, data: Windows, device: str) -> dict:
    model.eval()
    preds = []
    with torch.no_grad():
        for b in range(0, len(data), 1024):
            x, _ = data.get(np.arange(b, min(b + 1024, len(data))))
            preds.append(model(torch.from_numpy(x).to(device)).argmax(1).cpu().numpy())
    p, y = np.concatenate(preds), data.labels
    n = len(mmfit.CLASSES)
    confusion = np.zeros((n, n), dtype=np.int64)
    np.add.at(confusion, (y, p), 1)
    f1 = []
    for c in range(n):
        tp, fp, fn = confusion[c, c], confusion[:, c].sum() - confusion[c, c], confusion[c].sum() - confusion[c, c]
        f1.append(2 * tp / max(1, 2 * tp + fp + fn))
    exercise = y != mmfit.NONE
    return {"accuracy": float((p == y).mean()), "macro_f1": float(np.mean(f1)),
            "exercise_accuracy": float((p[exercise] == y[exercise]).mean()),
            "f1": dict(zip(mmfit.CLASSES, map(float, f1))), "confusion": confusion.tolist()}


def export(model: nn.Module, path: Path):
    model = model.cpu().eval()
    path.parent.mkdir(parents=True, exist_ok=True)
    x = torch.zeros(1, WINDOW, 17, 3)
    torch.onnx.export(model, (x,), str(path), input_names=["keypoints"], output_names=["logits"],
                      dynamic_axes={"keypoints": {0: "batch"}, "logits": {0: "batch"}}, opset_version=17, dynamo=False)
    path.with_suffix(".json").write_text(json.dumps({"classes": mmfit.CLASSES, "window": WINDOW, "fps": mmfit.FPS}, indent=1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default=str(ROOT / "models" / "exercise.onnx"))
    args = ap.parse_args()
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = "cuda" if torch.cuda.is_available() else "cpu"

    train = Windows(mmfit.TRAIN, stride=4, augment=True, rng=rng)
    val = Windows(mmfit.VAL, stride=8, augment=False, rng=rng)
    print(f"train windows {len(train)}, val {len(val)}, device {device}", flush=True)
    # Rest is most of a workout: sample classes evenly.
    counts = np.bincount(train.labels, minlength=len(mmfit.CLASSES))
    weights = 1.0 / counts[train.labels]
    weights /= weights.sum()

    model = ExerciseNet(len(mmfit.CLASSES)).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-2)
    steps = args.epochs * (len(train) // args.batch)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=2e-3, total_steps=steps)
    loss_fn = nn.CrossEntropyLoss(label_smoothing=0.05)
    best, best_state, history = -1.0, None, []
    for epoch in range(args.epochs):
        model.train()
        t0, total = time.time(), 0.0
        for _ in range(len(train) // args.batch):
            idx = rng.choice(len(train), args.batch, p=weights)
            x, y = train.get(idx)
            x, y = torch.from_numpy(x).to(device), torch.from_numpy(y).to(device)
            loss = loss_fn(model(x), y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
            total += loss.item()
        m = evaluate(model, val, device)
        history.append({"epoch": epoch, "loss": total / (len(train) // args.batch), "val_macro_f1": m["macro_f1"], "val_accuracy": m["accuracy"]})
        print(f"epoch {epoch:2d} loss {history[-1]['loss']:.3f} val macro-F1 {m['macro_f1']:.3f} acc {m['accuracy']:.3f} ({time.time() - t0:.0f}s)", flush=True)
        if m["macro_f1"] > best:
            best, best_state = m["macro_f1"], {k: v.detach().clone() for k, v in model.state_dict().items()}
    model.load_state_dict(best_state)
    results = {"val": evaluate(model, val, device), "history": history}
    for name, ids in (("test_seen", mmfit.TEST_SEEN), ("test_unseen", mmfit.TEST_UNSEEN)):
        results[name] = evaluate(model, Windows(ids, stride=8, augment=False, rng=rng), device)
        print(f"{name}: macro-F1 {results[name]['macro_f1']:.3f} accuracy {results[name]['accuracy']:.3f} "
              f"exercise accuracy {results[name]['exercise_accuracy']:.3f}", flush=True)
    export(model, Path(args.out))
    (ROOT / "results").mkdir(exist_ok=True)
    (ROOT / "results" / "classifier.json").write_text(json.dumps(results, indent=1))
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
