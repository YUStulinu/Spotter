#include "classifier.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <filesystem>
#include <stdexcept>

#include <onnxruntime_cxx_api.h>

namespace spotter {

struct Classifier::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "spotter-classifier"};
  std::unique_ptr<Ort::Session> session;
  std::vector<std::string> classes;
  int window = 64;
  std::vector<float> input;
};

Classifier::Classifier(const std::string& model) : impl_(std::make_unique<Impl>()) {
  std::filesystem::path path(model);
  if (!std::filesystem::exists(path)) throw std::runtime_error("missing model " + model + " (train it: see README)");
  QFile meta(QString::fromStdString(path.replace_extension(".json").string()));
  if (!meta.open(QIODevice::ReadOnly)) throw std::runtime_error("missing " + meta.fileName().toStdString());
  auto doc = QJsonDocument::fromJson(meta.readAll()).object();
  for (const auto& c : doc["classes"].toArray()) impl_->classes.push_back(c.toString().toStdString());
  impl_->window = doc["window"].toInt(64);
  Ort::SessionOptions o;
  o.SetIntraOpNumThreads(1);  // a tiny model: one thread leaves the rest to the pose model
  impl_->session = std::make_unique<Ort::Session>(impl_->env, path.replace_extension(".onnx").c_str(), o);
}

Classifier::~Classifier() = default;

int Classifier::window() const { return impl_->window; }

const std::vector<std::string>& Classifier::classes() const { return impl_->classes; }

Prediction Classifier::classify(const std::deque<Frame>& frames) {
  auto& m = *impl_;
  if (int(frames.size()) < m.window) return {};
  m.input.resize(size_t(m.window) * JointCount * 3);
  size_t i = 0;
  for (auto it = frames.end() - m.window; it != frames.end(); ++it)
    for (const auto& k : it->s.k) m.input[i++] = k.x, m.input[i++] = k.y, m.input[i++] = k.score;
  std::array<int64_t, 4> shape{1, m.window, JointCount, 3};
  auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  auto tensor = Ort::Value::CreateTensor<float>(mem, m.input.data(), m.input.size(), shape.data(), shape.size());
  const char* in[] = {"keypoints"};
  const char* out[] = {"logits"};
  auto result = m.session->Run(Ort::RunOptions{nullptr}, in, &tensor, 1, out, 1);
  const float* logits = result[0].GetTensorData<float>();
  Prediction p;
  p.p.resize(m.classes.size());
  float top = *std::max_element(logits, logits + m.classes.size()), sum = 0;
  for (size_t c = 0; c < m.classes.size(); ++c) sum += p.p[c] = std::exp(logits[c] - top);
  for (auto& v : p.p) v /= sum;
  p.best = int(std::max_element(p.p.begin(), p.p.end()) - p.p.begin());
  return p;
}

}  // namespace spotter
