#pragma once
// Form checks, one repetition at a time, from the 2D skeleton. Each check looks at the frames of a
// finished repetition and says what went wrong in a few spoken words. They only run where the
// camera can see the fault: knees caving in needs a view from the front, hips shooting up in a
// deadlift a view from the side.
#include <string>
#include <vector>

#include "reps.hpp"

namespace spotter {

enum class View { Front, Side, Unknown };
// From the shoulders' width against the torso's length: wide from the front, narrow from the side.
View view_of(const Skeleton& s);

struct Fault {
  std::string id;      // knees_in, depth, hips_first, lockout, uneven, swing, too_high, sag
  std::string cue;     // what the coach says
  std::string detail;  // what was measured
  double t = 0;
  float value = 0;
};

struct FormLimits {
  float knee_ratio = 0.78f;    // knees apart / ankles apart at the bottom of a squat or lunge
  float depth = 0.30f;         // hips above knees at the bottom, torso lengths
  float hips_first = 1.8f;     // hips rising this many times faster than the shoulders
  float lockout = 160.f;       // hip angle at the top of a deadlift
  float press_lockout = 150.f; // elbow angle at the top of a press
  float uneven = 0.25f;        // wrists at different heights, torso lengths
  float swing = 14.f;          // torso lean changing during a curl or a raise, degrees
  float too_high = 115.f;      // lateral raise above the shoulders, degrees
  float sag = 160.f;           // shoulder-hip-ankle in a push-up, degrees
};

// The checks for one repetition; `frames` covers rep.start .. rep.end (resampled, smoothed).
std::vector<Fault> check_form(const Exercise& e, const Rep& rep, const std::vector<Frame>& frames,
                              const FormLimits& limits = {});

}  // namespace spotter
