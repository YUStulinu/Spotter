#pragma once
// Counting repetitions. Every exercise has one signal that goes out and back once per repetition
// (the hips going down and up in a squat, the elbows bending in a curl), measured in body units so
// that it does not depend on the camera. The counter is a Schmitt trigger around where the set
// started: a repetition is the signal leaving that position by more than a share of the exercise's
// usual range and coming back. The range adapts to the lifter after each repetition.
#include <optional>
#include <string>
#include <vector>

#include "skeleton.hpp"

namespace spotter {

enum class Signal {
  HipHeight,   // hips above ankles, torso lengths
  KneeAngle,   // degrees
  ElbowAngle,  // degrees
  HipAngle,    // shoulder-hip-knee, degrees
  WristLift,   // wrists above shoulders, torso lengths
  ArmRaise,    // arms from the sides, degrees
  Stance,      // ankles apart, torso lengths
  ShoulderHeight,  // mid-shoulders above mid-ankles (or hips when the ankles are hidden), torso lengths
  NoseHeight,  // nose above mid-hips, torso lengths
  Count
};
const char* signal_name(Signal s);
// The signal in a frame; `torso` is the lifter's standing torso length in pixels.
std::optional<float> measure(Signal sig, const Skeleton& s, float torso);

struct Exercise {
  std::string id;      // as the classifier names it
  std::string name;    // for people
  Signal signal;
  float direction;     // +1: a repetition raises the signal, -1: lowers it
  float amplitude;     // the usual range of one repetition, in the signal's units
  bool count_at_far;   // count when the far end is reached (deadlift lockout), not on the way back
  double min_rep_s;    // faster than this is noise
};

const std::vector<Exercise>& exercises();
const Exercise* find_exercise(const std::string& id);

struct Rep {
  double start = 0, far = 0, end = 0;  // seconds: leaving, the far end, back
  float range = 0;                     // how far the signal went, in its units
};

class RepCounter {
 public:
  explicit RepCounter(const Exercise& e) : e_(e) {}
  // Feeds one frame (any rate); returns a repetition when one has just been completed.
  std::optional<Rep> add(double t, const Skeleton& s);
  int count() const { return int(reps_.size()); }
  const std::vector<Rep>& reps() const { return reps_; }
  const Exercise& exercise() const { return e_; }
  // How far into the current repetition, 0 (at rest) .. 1 (the far end), for the UI.
  float progress() const { return progress_; }
  void reset();

 private:
  const Exercise& e_;
  std::vector<Rep> reps_;
  float torso_ = 0;      // the largest torso length seen: standing tall
  float smooth_ = 0;     // the smoothed signal
  bool have_ = false;
  float rest_ = 0;       // where the set started (moves slowly)
  float range_ = 0;      // the expected range of a repetition
  bool out_ = false;     // past the far threshold
  bool counted_ = false; // counted at the far end already
  double left_ = 0, far_t_ = 0, last_t_ = 0;
  float extreme_ = 0;
  float trough_ = 0;     // the lowest point since the last repetition, towards the far end
  float progress_ = 0;
};

}  // namespace spotter
