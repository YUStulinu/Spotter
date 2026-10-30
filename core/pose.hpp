#pragma once
// The skeleton in a camera frame: a person detector (YOLOX-tiny) finds the lifter, a top-down pose
// model (RTMPose-m) puts 17 COCO keypoints on them. Once found, the person is tracked from the last
// skeleton's box and the detector runs again only when the skeleton gets unsure.
#include <array>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

namespace spotter {

// COCO order.
enum Joint {
  Nose, LEye, REye, LEar, REar, LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist,
  LHip, RHip, LKnee, RKnee, LAnkle, RAnkle, JointCount
};

struct Keypoint {
  float x = 0, y = 0, score = 0;
};

struct Skeleton {
  std::array<Keypoint, JointCount> k{};
  float score = 0;  // mean keypoint score; 0 when nobody was found
  bool found() const { return score > 0; }
};

struct PoseTiming {
  double detect_ms = 0, pose_ms = 0;
  bool detected = false;  // the detector ran for this frame
};

class PoseEstimator {
 public:
  // models: the folder with yolox-tiny.onnx and rtmpose-m.onnx. gpu: DirectML on Windows when it is
  // available (falls back to the CPU).
  PoseEstimator(const std::string& models, bool gpu = true);
  ~PoseEstimator();
  PoseEstimator(const PoseEstimator&) = delete;
  PoseEstimator& operator=(const PoseEstimator&) = delete;

  Skeleton run(const cv::Mat& bgr, PoseTiming* timing = nullptr);
  void reset();                       // forget the tracked person (a new video, a new set)
  const std::string& device() const;  // "DirectML" or "CPU"

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spotter
