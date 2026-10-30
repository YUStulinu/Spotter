#pragma once
// Synthetic people for the tests: a stick figure facing the camera (or seen from the side) whose
// joints follow an exercise's motion, so the counter and the checks can be tested without video.
#include <cmath>
#include <numbers>
#include <vector>

#include "skeleton.hpp"

namespace spotter::synth {

// A standing person, front view, 1080 px tall frame: hips at y=600, torso 200 px.
inline Skeleton standing(bool side = false) {
  Skeleton s;
  auto set = [&](int j, float x, float y) { s.k[size_t(j)] = {x, y, 0.9f}; };
  float w = side ? 0.15f : 1.f;  // seen from the side, left and right overlap
  set(Nose, 640, 330);
  set(LEye, 640 + 10 * w, 320), set(REye, 640 - 10 * w, 320);
  set(LEar, 640 + 22 * w, 325), set(REar, 640 - 22 * w, 325);
  set(LShoulder, 640 + 70 * w, 400), set(RShoulder, 640 - 70 * w, 400);
  set(LElbow, 640 + 80 * w, 510), set(RElbow, 640 - 80 * w, 510);
  set(LWrist, 640 + 85 * w, 610), set(RWrist, 640 - 85 * w, 610);
  set(LHip, 640 + 45 * w, 600), set(RHip, 640 - 45 * w, 600);
  set(LKnee, 640 + 55 * w, 780), set(RKnee, 640 - 55 * w, 780);
  set(LAnkle, 640 + 60 * w, 960), set(RAnkle, 640 - 60 * w, 960);
  s.score = 0.9f;
  return s;
}

// A squat at depth d (0 standing .. 1 hips at knee height); `cave` pulls the knees in at depth.
inline Skeleton squat_with(float d, float cave) {
  Skeleton s = standing();
  float drop = 170 * d;
  for (int j : {Nose, LEye, REye, LEar, REar, LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist, LHip, RHip})
    s.k[size_t(j)].y += drop;
  float out = 30 * d * (1 - cave) - 40 * d * cave;
  s.k[LKnee].x += out, s.k[RKnee].x -= out;
  s.k[LKnee].y += drop * 0.15f, s.k[RKnee].y += drop * 0.15f;
  return s;
}

inline Skeleton squat(float d) { return squat_with(d, 0); }

// A biceps curl: the forearms rotate up by `d` (0 .. 1).
inline Skeleton curl(float d) {
  Skeleton s = standing();
  float a = d * 2.4f;  // radians from hanging
  for (auto [el, wr] : {std::pair{LElbow, LWrist}, std::pair{RElbow, RWrist}}) {
    s.k[wr].x = s.k[el].x + 100 * std::sin(a) * 0.2f;
    s.k[wr].y = s.k[el].y + 100 * std::cos(a);
  }
  return s;
}

// Repetitions of a motion: rest, then n cycles of `period` seconds, then rest, at `rate` Hz.
template <class F>
std::vector<Frame> reps(F pose, int n, double period, double rate = 30, double rest = 1.5) {
  std::vector<Frame> out;
  double total = rest + n * period + rest;
  for (int i = 0; i * (1 / rate) < total; ++i) {
    double t = i / rate;
    float d = 0;
    if (t > rest && t < rest + n * period) d = float(0.5 - 0.5 * std::cos(2 * std::numbers::pi * (t - rest) / period));
    out.push_back({t, pose(d)});
  }
  return out;
}

}  // namespace spotter::synth
