#pragma once
// Which exercise is being done: the last 64 frames (2.1 s at 30 Hz) of raw keypoints through the
// model trained in train/ (models/exercise.onnx, normalisation included).
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "skeleton.hpp"

namespace spotter {

struct Prediction {
  std::vector<float> p;  // per class
  int best = -1;
  float confidence() const { return best >= 0 ? p[best] : 0; }
};

class Classifier {
 public:
  explicit Classifier(const std::string& model);  // path of exercise.onnx (exercise.json beside it)
  ~Classifier();
  Classifier(const Classifier&) = delete;
  Classifier& operator=(const Classifier&) = delete;

  int window() const;
  const std::vector<std::string>& classes() const;
  // The last window() frames of the track; an empty prediction until there are enough.
  Prediction classify(const std::deque<Frame>& frames);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spotter
