#include "reps.hpp"

#include <algorithm>
#include <cmath>

namespace spotter {

const char* signal_name(Signal s) {
  static const char* names[] = {"hip_height", "knee_angle", "elbow_angle", "hip_angle", "wrist_lift",
                                "arm_raise", "stance", "shoulder_height", "nose_height"};
  return names[int(s)];
}

std::optional<float> measure(Signal sig, const Skeleton& s, float torso) {
  const auto& k = s.k;
  switch (sig) {
    case Signal::HipHeight: return Body::hip_height(s, torso);
    case Signal::KneeAngle: return Body::knee_angle(s);
    case Signal::ElbowAngle: return Body::elbow_angle(s);
    case Signal::HipAngle: return Body::hip_angle(s);
    case Signal::WristLift: return Body::wrist_lift(s, torso);
    case Signal::ArmRaise: return Body::arm_raise(s);
    case Signal::Stance: return Body::stance(s, torso);
    case Signal::ShoulderHeight:
      if (torso <= 0 || !seen(k[LShoulder]) || !seen(k[RShoulder]) || !seen(k[LAnkle]) || !seen(k[RAnkle])) return std::nullopt;
      return (mid(k[LAnkle], k[RAnkle]).y - mid(k[LShoulder], k[RShoulder]).y) / torso;
    case Signal::NoseHeight:
      if (torso <= 0 || !seen(k[Nose]) || !seen(k[LHip]) || !seen(k[RHip])) return std::nullopt;
      return (mid(k[LHip], k[RHip]).y - k[Nose].y) / torso;
    case Signal::Count: break;
  }
  return std::nullopt;
}

// Signals and ranges chosen on MM-Fit's training workouts (train/spotter_train/tune_reps.py); the
// deadlift, which MM-Fit does not have, by hand.
const std::vector<Exercise>& exercises() {
  static const std::vector<Exercise> all = {
#include "exercises.inc"
  };
  return all;
}

const Exercise* find_exercise(const std::string& id) {
  for (const auto& e : exercises())
    if (e.id == id) return &e;
  return nullptr;
}

void RepCounter::reset() {
  reps_.clear();
  torso_ = 0;
  have_ = out_ = counted_ = false;
  progress_ = 0;
}

std::optional<Rep> RepCounter::add(double t, const Skeleton& s) {
  if (!s.found()) return std::nullopt;
  float torso = Body::torso(s);
  // The standing torso: lying down or bending over shortens it on screen.
  if (torso > torso_) torso_ = torso_ == 0 ? torso : torso_ + 0.3f * (torso - torso_);
  auto v = measure(e_.signal, s, torso_);
  if (!v) return std::nullopt;
  double dt = have_ ? std::clamp(t - last_t_, 0.0, 0.5) : 0;
  last_t_ = t;
  if (!have_) {
    smooth_ = rest_ = *v;
    range_ = e_.amplitude;
    trough_ = 0;
    have_ = true;
    left_ = t;
    return std::nullopt;
  }
  float a = float(1 - std::exp(-dt / 0.08));
  smooth_ += a * (*v - smooth_);
  float x = e_.direction * (smooth_ - rest_);  // towards the far end
  progress_ = std::clamp(x / range_, 0.f, 1.f);
  std::optional<Rep> done;
  if (!out_) {
    if (x < 0) rest_ += 0.3f * (smooth_ - rest_);           // further back than the start: that is the start
    else if (x < 0.15f * range_) rest_ += 0.01f * (smooth_ - rest_);  // standing still: settle
    x = e_.direction * (smooth_ - rest_);
    // The turning point since the last repetition: the next one starts from there, even when the
    // lifter does not come all the way back to where the set began.
    if (x <= trough_) trough_ = x, left_ = t;
    if (x - trough_ > 0.55f * range_) {
      rest_ += e_.direction * trough_;
      x -= trough_;
      out_ = true;
      counted_ = false;
      extreme_ = x;
      far_t_ = t;
    }
  } else {
    if (x > extreme_) extreme_ = x, far_t_ = t;
    if (e_.count_at_far && !counted_ && x > 0.8f * range_ && t - left_ >= e_.min_rep_s / 2) {
      counted_ = true;
      reps_.push_back({left_, t, t, x});
      done = reps_.back();
    }
    // Back: near the start, or most of a repetition's range back from the far end.
    if (x < std::max(0.3f * range_, extreme_ - 0.75f * range_)) {
      out_ = false;
      trough_ = x;
      if (!e_.count_at_far && t - left_ >= e_.min_rep_s) {
        reps_.push_back({left_, far_t_, t, extreme_});
        done = reps_.back();
      } else if (e_.count_at_far && counted_) {
        reps_.back().end = t;
        reps_.back().range = extreme_;
      }
      range_ = std::clamp(0.7f * range_ + 0.3f * extreme_, 0.5f * e_.amplitude, 2.0f * e_.amplitude);
      left_ = t;
    }
  }
  return done;
}

}  // namespace spotter
