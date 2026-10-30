#pragma once
// Skeletons over time: smoothing, resampling to a fixed rate (the camera's rate varies, the
// classifier wants 30 Hz), and the joint geometry the repetition counter and the form checks use.
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "pose.hpp"

namespace spotter {

constexpr float kMinScore = 0.3f;  // below this a keypoint does not count as seen
constexpr double kRate = 30.0;     // Hz, the classifier's and the counter's rate

struct Point {
  float x = 0, y = 0;
};

inline bool seen(const Keypoint& k) { return k.score >= kMinScore; }
Point mid(const Keypoint& a, const Keypoint& b);
float dist(Point a, Point b);
// The angle at b, in degrees, between b->a and b->c.
float angle(Point a, Point b, Point c);
inline Point pt(const Keypoint& k) { return {k.x, k.y}; }

// A skeleton at a time, in seconds.
struct Frame {
  double t = 0;
  Skeleton s;
};

// The One Euro filter (Casiez et al., CHI 2012), per coordinate: little smoothing when a joint moves
// fast, more when it is still, so the skeleton is steady without lagging behind a repetition.
class Smoother {
 public:
  explicit Smoother(double min_cutoff = 1.5, double beta = 0.02) : min_cutoff_(min_cutoff), beta_(beta) {}
  Skeleton operator()(double t, const Skeleton& s);
  void reset() { have_ = false; }

 private:
  struct Axis {
    double x = 0, dx = 0;
  };
  double min_cutoff_, beta_, last_t_ = 0;
  bool have_ = false;
  std::array<Axis, 2 * JointCount> axes_{};
};

// Frames resampled to kRate, newest last, at most `capacity` of them. A frame without a person is
// kept (all scores 0); a gap longer than a second clears the track.
class Track {
 public:
  explicit Track(size_t capacity = 64 * 4) : capacity_(capacity) {}
  void add(double t, const Skeleton& s);
  const std::deque<Frame>& frames() const { return frames_; }
  void clear();

 private:
  size_t capacity_;
  std::deque<Frame> frames_;
  std::optional<Frame> last_;
};

// Body-relative measures, in body units so that they do not depend on the distance to the camera.
struct Body {
  // Torso length (mid-shoulders to mid-hips) when standing tall; 0 if not seen.
  static float torso(const Skeleton& s);
  // Height of the hips above the ankles, in torso lengths.
  static std::optional<float> hip_height(const Skeleton& s, float torso);
  static std::optional<float> knee_angle(const Skeleton& s);   // the smaller of the two seen
  static std::optional<float> elbow_angle(const Skeleton& s);  // mean of the seen ones
  static std::optional<float> hip_angle(const Skeleton& s);    // shoulder-hip-knee
  // Wrists above the shoulders, in torso lengths (positive = above).
  static std::optional<float> wrist_lift(const Skeleton& s, float torso);
  // How far the arms are raised sideways, 0 (down) .. 180 (overhead), mean of both.
  static std::optional<float> arm_raise(const Skeleton& s);
  // Ankles apart, in torso lengths.
  static std::optional<float> stance(const Skeleton& s, float torso);
  // Torso lean from vertical, degrees.
  static std::optional<float> torso_lean(const Skeleton& s);
};

// The keypoint text file shared with the Python side: "t x y s x y s ..." per line.
std::vector<Frame> read_keypoints(const std::string& path);
void write_keypoints(const std::string& path, const std::vector<Frame>& frames);

}  // namespace spotter
