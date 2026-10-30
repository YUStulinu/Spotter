"""The training side without MM-Fit itself: the normalisation inside the model, the ONNX export,
the conversion from OpenPose's joints and the shared keypoint file."""

import numpy as np
import onnxruntime as ort
import torch

from spotter_train import mmfit
from spotter_train.model import WINDOW, ExerciseNet, normalise
from spotter_train.train import MIRROR, export, window_labels


def person(t: int = WINDOW, seed: int = 0) -> np.ndarray:
    """A standing figure in pixels that bobs up and down, (t, 17, 3)."""
    rng = np.random.default_rng(seed)
    base = np.array([[640, 330], [650, 320], [630, 320], [662, 325], [618, 325], [710, 400], [570, 400], [720, 510],
                     [560, 510], [725, 610], [555, 610], [685, 600], [595, 600], [695, 780], [585, 780], [700, 960],
                     [580, 960]], dtype=np.float32)
    k = np.zeros((t, 17, 3), dtype=np.float32)
    for i in range(t):
        k[i, :, :2] = base + [0, 60 * np.sin(i / 8)] + rng.normal(0, 1, (17, 2))
        k[i, :, 2] = 0.9
    return k


def test_normalisation_ignores_place_and_size():
    k = torch.from_numpy(person()[None])
    moved = k.clone()
    moved[..., :2] = moved[..., :2] * 0.5 + torch.tensor([100.0, -40.0])
    assert torch.allclose(normalise(k), normalise(moved), atol=1e-4)


def test_normalisation_keeps_movement_and_masks_unsure_joints():
    k = torch.from_numpy(person()[None])
    x = normalise(k)  # (1, 51, t): the 17 joints' x, their y, then their visibility
    nose_y = x[0, 17]
    assert nose_y.max() - nose_y.min() > 0.3  # the bobbing is still there, in torso lengths
    k[0, :, 9, 2] = 0.1  # the left wrist unsure
    x = normalise(k)
    assert torch.all(x[0, 9] == 0) and torch.all(x[0, 17 + 9] == 0) and torch.all(x[0, 34 + 9] == 0)


def test_no_person_gives_zeros_not_nan():
    k = torch.zeros(1, WINDOW, 17, 3)
    x = normalise(k)
    assert torch.isfinite(x).all() and (x == 0).all()


def test_onnx_matches_torch(tmp_path):
    torch.manual_seed(0)
    model = ExerciseNet(len(mmfit.CLASSES)).eval()
    path = tmp_path / "exercise.onnx"
    export(model, path)
    x = np.stack([person(seed=1), person(seed=2)])
    want = model(torch.from_numpy(x)).detach().numpy()
    got = ort.InferenceSession(str(path)).run(None, {"keypoints": x})[0]
    assert np.allclose(want, got, atol=1e-4)
    meta = path.with_suffix(".json").read_text()
    assert '"window": 64' in meta and "jumping_jacks" in meta


def test_openpose_to_coco():
    # OpenPose order: nose, neck, r-shoulder ... ; COCO wants l-shoulder (5) from OpenPose's 5 and r-shoulder (6) from 2.
    assert mmfit.OPENPOSE_TO_COCO[5] == 5 and mmfit.OPENPOSE_TO_COCO[6] == 2
    assert 1 not in mmfit.OPENPOSE_TO_COCO  # the neck has no COCO joint
    assert sorted(mmfit.OPENPOSE_TO_COCO) == [0] + list(range(2, 18))


def test_mirror_swaps_left_and_right():
    assert [MIRROR[i] for i in MIRROR] == list(range(17))
    assert MIRROR[5] == 6 and MIRROR[15] == 16 and MIRROR[0] == 0


def test_window_label_is_the_newest_half():
    y = np.full(200, mmfit.NONE)
    y[100:] = 0  # squats from frame 100
    starts = np.array([0, 60, 100])
    assert list(window_labels(y, starts)) == [mmfit.NONE, 0, 0]


def test_keypoint_file_round_trip(tmp_path):
    k = person(10)
    t = np.arange(10) / 30
    mmfit.write_keypoints(tmp_path / "a.kp", t, k)
    t2, k2 = mmfit.read_keypoints(tmp_path / "a.kp")
    assert np.allclose(t, t2, atol=1e-4) and np.allclose(k, k2, atol=0.01)
