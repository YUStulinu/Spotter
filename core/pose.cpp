#include "pose.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>
#ifdef SPOTTER_DML
#include <dml_provider_factory.h>
#endif

namespace spotter {
namespace {

constexpr int kDetSize = 416;              // YOLOX input, letterboxed
constexpr int kPoseW = 192, kPoseH = 256;  // RTMPose input
constexpr float kSimccSplit = 2.0f;        // SimCC bins per pixel
constexpr float kBoxPadding = 1.25f;
constexpr float kDetectBelow = 0.45f;      // run the detector again under this mean keypoint score
constexpr int kDetectEvery = 60;           // and at least this often, frames

double ms_since(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

struct Box {
  float x1, y1, x2, y2, score;
};

}  // namespace

struct PoseEstimator::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "spotter"};
  std::unique_ptr<Ort::Session> det, pose;
  std::string device = "CPU";
  Box tracked{};
  bool tracking = false;
  int since_detect = 0;

  std::unique_ptr<Ort::Session> open(const std::filesystem::path& path, bool gpu) {
    if (!std::filesystem::exists(path)) throw std::runtime_error("missing model " + path.string() + " (run scripts/fetch-deps.sh)");
    Ort::SessionOptions o;
    o.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef SPOTTER_DML
    if (gpu) {
      try {
        o.DisableMemPattern();
        o.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(o, 0));
        auto s = std::make_unique<Ort::Session>(env, path.c_str(), o);
        device = "DirectML";
        return s;
      } catch (const Ort::Exception&) {
        o = Ort::SessionOptions();
        o.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      }
    }
#else
    (void)gpu;
#endif
    o.SetIntraOpNumThreads(4);
    return std::make_unique<Ort::Session>(env, path.c_str(), o);
  }

  // Highest-scoring person, preferring one that overlaps the tracked box.
  bool detect(const cv::Mat& bgr, Box& out) {
    float scale = std::min(float(kDetSize) / bgr.cols, float(kDetSize) / bgr.rows);
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(int(bgr.cols * scale), int(bgr.rows * scale)));
    cv::Mat canvas(kDetSize, kDetSize, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(0, 0, resized.cols, resized.rows)));
    std::vector<float> input(3 * kDetSize * kDetSize);
    for (int y = 0; y < kDetSize; ++y) {
      const auto* row = canvas.ptr<cv::Vec3b>(y);
      for (int x = 0; x < kDetSize; ++x)
        for (int c = 0; c < 3; ++c) input[(c * kDetSize + y) * kDetSize + x] = row[x][c];  // BGR, 0-255
    }
    std::array<int64_t, 4> shape{1, 3, kDetSize, kDetSize};
    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
    const char* in[] = {"input"};
    const char* outs[] = {"dets", "labels"};
    auto result = det->Run(Ort::RunOptions{nullptr}, in, &tensor, 1, outs, 2);
    auto dshape = result[0].GetTensorTypeAndShapeInfo().GetShape();
    const float* d = result[0].GetTensorData<float>();
    const int64_t* labels = result[1].GetTensorData<int64_t>();
    bool any = false;
    float best = 0;
    for (int64_t i = 0; i < dshape[1]; ++i) {
      const float* r = d + i * 5;
      if (labels[i] != 0 || r[4] < 0.3f) continue;
      Box b{r[0] / scale, r[1] / scale, r[2] / scale, r[3] / scale, r[4]};
      float rank = b.score;
      if (tracking) {
        float ix = std::max(0.f, std::min(b.x2, tracked.x2) - std::max(b.x1, tracked.x1));
        float iy = std::max(0.f, std::min(b.y2, tracked.y2) - std::max(b.y1, tracked.y1));
        float uni = (b.x2 - b.x1) * (b.y2 - b.y1) + (tracked.x2 - tracked.x1) * (tracked.y2 - tracked.y1) - ix * iy;
        rank += uni > 0 ? ix * iy / uni : 0;
      } else {
        rank += 0.5f * (b.x2 - b.x1) * (b.y2 - b.y1) / float(bgr.cols * bgr.rows);  // the lifter fills the frame
      }
      if (rank > best) best = rank, out = b, any = true;
    }
    return any;
  }

  Skeleton estimate(const cv::Mat& bgr, const Box& box) {
    // The box's centre and size, padded and stretched to the model's aspect ratio.
    float cx = (box.x1 + box.x2) / 2, cy = (box.y1 + box.y2) / 2;
    float w = (box.x2 - box.x1) * kBoxPadding, h = (box.y2 - box.y1) * kBoxPadding;
    float aspect = float(kPoseW) / kPoseH;
    if (w > h * aspect) h = w / aspect; else w = h * aspect;
    cv::Mat warp = (cv::Mat_<float>(2, 3) << kPoseW / w, 0, kPoseW / 2.f - cx * kPoseW / w,
                                             0, kPoseH / h, kPoseH / 2.f - cy * kPoseH / h);
    cv::Mat crop;
    cv::warpAffine(bgr, crop, warp, cv::Size(kPoseW, kPoseH), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    static const float mean[3] = {123.675f, 116.28f, 103.53f}, stdev[3] = {58.395f, 57.12f, 57.375f};
    std::vector<float> input(3 * kPoseW * kPoseH);
    for (int y = 0; y < kPoseH; ++y) {
      const auto* row = crop.ptr<cv::Vec3b>(y);
      for (int x = 0; x < kPoseW; ++x)
        for (int c = 0; c < 3; ++c)  // to RGB
          input[(c * kPoseH + y) * kPoseW + x] = (row[x][2 - c] - mean[c]) / stdev[c];
    }
    std::array<int64_t, 4> shape{1, 3, kPoseH, kPoseW};
    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
    const char* in[] = {"input"};
    const char* outs[] = {"simcc_x", "simcc_y"};
    auto result = pose->Run(Ort::RunOptions{nullptr}, in, &tensor, 1, outs, 2);
    const float* sx = result[0].GetTensorData<float>();
    const float* sy = result[1].GetTensorData<float>();
    const int nx = int(kPoseW * kSimccSplit), ny = int(kPoseH * kSimccSplit);
    Skeleton s;
    float total = 0;
    for (int j = 0; j < JointCount; ++j) {
      const float* px = sx + j * nx;
      const float* py = sy + j * ny;
      int bx = int(std::max_element(px, px + nx) - px);
      int by = int(std::max_element(py, py + ny) - py);
      float score = std::min(px[bx], py[by]);
      // Back from the crop to the frame.
      float x = bx / kSimccSplit, y = by / kSimccSplit;
      s.k[j] = {cx + (x - kPoseW / 2.f) * w / kPoseW, cy + (y - kPoseH / 2.f) * h / kPoseH, std::clamp(score, 0.f, 1.f)};
      total += s.k[j].score;
    }
    s.score = total / float(JointCount);
    return s;
  }

  // The next frame's box: around the confident keypoints, a little larger than the body.
  static Box box_of(const Skeleton& s, const cv::Mat& frame) {
    float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
    for (const auto& k : s.k) {
      if (k.score < 0.3f) continue;
      x1 = std::min(x1, k.x), y1 = std::min(y1, k.y), x2 = std::max(x2, k.x), y2 = std::max(y2, k.y);
    }
    float w = x2 - x1, h = y2 - y1;
    x1 -= 0.1f * w, x2 += 0.1f * w, y1 -= 0.15f * h, y2 += 0.1f * h;  // the keypoints stop at the eyes and ankles
    return {std::max(0.f, x1), std::max(0.f, y1), std::min(float(frame.cols), x2), std::min(float(frame.rows), y2), s.score};
  }
};

PoseEstimator::PoseEstimator(const std::string& models, bool gpu) : impl_(std::make_unique<Impl>()) {
  std::filesystem::path dir(models);
  impl_->det = impl_->open(dir / "yolox-tiny.onnx", gpu);
  impl_->pose = impl_->open(dir / "rtmpose-m.onnx", gpu);
}

PoseEstimator::~PoseEstimator() = default;

const std::string& PoseEstimator::device() const { return impl_->device; }

void PoseEstimator::reset() {
  impl_->tracking = false;
  impl_->since_detect = 0;
}

Skeleton PoseEstimator::run(const cv::Mat& bgr, PoseTiming* timing) {
  auto& m = *impl_;
  PoseTiming t;
  Box box = m.tracked;
  if (!m.tracking || m.since_detect >= kDetectEvery) {
    auto t0 = std::chrono::steady_clock::now();
    Box found{};
    bool ok = m.detect(bgr, found);
    t.detect_ms = ms_since(t0);
    t.detected = true;
    m.since_detect = 0;
    if (!ok) {
      m.tracking = false;
      if (timing) *timing = t;
      return {};
    }
    box = found;
  }
  auto t0 = std::chrono::steady_clock::now();
  Skeleton s = m.estimate(bgr, box);
  t.pose_ms = ms_since(t0);
  if (s.score < kDetectBelow && !t.detected) {
    // Lost the person: look again in this frame.
    t0 = std::chrono::steady_clock::now();
    Box found{};
    t.detected = true;
    bool ok = m.detect(bgr, found);
    t.detect_ms = ms_since(t0);
    m.since_detect = 0;
    if (ok) {
      t0 = std::chrono::steady_clock::now();
      s = m.estimate(bgr, found);
      t.pose_ms += ms_since(t0);
    }
  }
  m.since_detect++;
  m.tracking = s.score >= kDetectBelow * 0.7f;
  if (m.tracking) m.tracked = Impl::box_of(s, bgr);
  if (timing) *timing = t;
  return s;
}

}  // namespace spotter
