"""The exercise classifier: about two seconds of skeletons in, the exercise out.

The input is raw keypoints in pixels, (batch, 64 frames at 30 Hz, 17 joints, x/y/score), exactly as
the C++ side tracks them. Normalising them is part of the model, so the ONNX file is the only
definition of the preprocessing: joints below a score of 0.3 are masked; the window is centred on
the mean hip position and scaled by the mean torso length, so position in the frame, distance from
the camera and body size drop out, while the movement inside the window stays.
"""

from __future__ import annotations

import torch
from torch import nn

WINDOW = 64
JOINTS = 17
MIN_SCORE = 0.3
L_SHOULDER, R_SHOULDER, L_HIP, R_HIP = 5, 6, 11, 12


def normalise(kp: torch.Tensor) -> torch.Tensor:
    """(b, t, 17, 3) pixels -> (b, 3 * 17, t): x, y in torso lengths around the hips, and a visibility mask."""
    xy, score = kp[..., :2], kp[..., 2:]
    seen = (score >= MIN_SCORE).float()                       # (b, t, 17, 1)

    def mean_point(a: int, b: int) -> tuple[torch.Tensor, torch.Tensor]:
        w = seen[:, :, a] * seen[:, :, b]                     # both joints of the pair seen, (b, t, 1)
        return (xy[:, :, a] + xy[:, :, b]) / 2, w

    hips, wh = mean_point(L_HIP, R_HIP)
    shoulders, ws = mean_point(L_SHOULDER, R_SHOULDER)
    eps = 1e-6
    centre = (hips * wh).sum(1, keepdim=True) / (wh.sum(1, keepdim=True) + eps)            # (b, 1, 2)
    wt = wh * ws
    torso = ((shoulders - hips).norm(dim=-1, keepdim=True) * wt).sum(1, keepdim=True) / (wt.sum(1, keepdim=True) + eps)
    # Without both hips and shoulders, fall back to the middle and the extent of what is seen.
    seen_mean = (xy * seen).sum(dim=(1, 2)) / (seen.sum(dim=(1, 2)) + eps)                  # (b, 2)
    dev = (xy - seen_mean[:, None, None, :]).abs().amax(-1, keepdim=True) * seen            # (b, t, 17, 1)
    spread = dev.amax(dim=(1, 2))                                                            # (b, 1)
    torso = torch.where(torso > 1.0, torso, spread.unsqueeze(1) / 2 + 1.0)                # (b, 1, 1)
    any_hips = (wh.sum(1, keepdim=True) > 0).float()
    centre = any_hips * centre + (1 - any_hips) * seen_mean.unsqueeze(1)
    norm = (xy - centre.unsqueeze(2)) / torso.unsqueeze(2) * seen                           # (b, t, 17, 2)
    x = torch.cat([norm, seen], dim=-1)                                                     # (b, t, 17, 3)
    b, t = x.shape[:2]
    return x.permute(0, 3, 2, 1).reshape(b, 3 * JOINTS, t)


class Block(nn.Module):
    def __init__(self, cin: int, cout: int, dilation: int):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv1d(cin, cout, 3, padding=dilation, dilation=dilation), nn.BatchNorm1d(cout), nn.GELU(),
            nn.Conv1d(cout, cout, 3, padding=dilation, dilation=dilation), nn.BatchNorm1d(cout))
        self.skip = nn.Conv1d(cin, cout, 1) if cin != cout else nn.Identity()
        self.act = nn.GELU()

    def forward(self, x):
        return self.act(self.conv(x) + self.skip(x))


class ExerciseNet(nn.Module):
    """A small temporal convolution network: velocities are added to positions, four dilated residual
    blocks, then mean and max over time."""

    def __init__(self, classes: int, width: int = 96, dropout: float = 0.2):
        super().__init__()
        cin = 3 * JOINTS + 2 * JOINTS
        self.stem = nn.Sequential(nn.Conv1d(cin, width, 1), nn.BatchNorm1d(width), nn.GELU())
        self.blocks = nn.Sequential(*[Block(width, width, d) for d in (1, 2, 4, 8)])
        self.head = nn.Sequential(nn.Dropout(dropout), nn.Linear(2 * width, classes))

    def forward(self, kp: torch.Tensor) -> torch.Tensor:
        x = normalise(kp)                                     # (b, 51, t)
        pos = x[:, : 2 * JOINTS]
        vel = torch.cat([torch.zeros_like(pos[..., :1]), pos[..., 1:] - pos[..., :-1]], dim=-1) * 10
        h = self.blocks(self.stem(torch.cat([x, vel], dim=1)))
        return self.head(torch.cat([h.mean(-1), h.amax(-1)], dim=1))
