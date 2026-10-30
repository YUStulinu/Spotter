#include "skeleton.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace spotter {

Point mid(const Keypoint& a, const Keypoint& b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }

float dist(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }

float angle(Point a, Point b, Point c) {
  float ux = a.x - b.x, uy = a.y - b.y, vx = c.x - b.x, vy = c.y - b.y;
  float n = std::hypot(ux, uy) * std::hypot(vx, vy);
  if (n <= 0) return 180;
  float cosv = std::clamp((ux * vx + uy * vy) / n, -1.f, 1.f);
  return std::acos(cosv) * 180.f / std::numbers::pi_v<float>;
}

// --- smoothing -------------------------------------------------------------------------------------

namespace {
double alpha(double cutoff, double dt) {
  double tau = 1.0 / (2 * std::numbers::pi * cutoff);
  return 1.0 / (1.0 + tau / dt);
}
}  // namespace

Skeleton Smoother::operator()(double t, const Skeleton& s) {
  if (!s.found()) {
    have_ = false;
    return s;
  }
  Skeleton out = s;
  double dt = have_ ? t - last_t_ : 0;
  if (!have_ || dt <= 0 || dt > 0.5) {
    for (int j = 0; j < JointCount; ++j) axes_[2 * j] = {s.k[j].x, 0}, axes_[2 * j + 1] = {s.k[j].y, 0};
    have_ = true;
    last_t_ = t;
    return out;
  }
  for (int j = 0; j < JointCount; ++j) {
    for (int c = 0; c < 2; ++c) {
      Axis& a = axes_[2 * j + c];
      double x = c == 0 ? s.k[j].x : s.k[j].y;
      double dx = (x - a.x) / dt;
      double ad = alpha(1.0, dt);
      a.dx = ad * dx + (1 - ad) * a.dx;
      double cutoff = min_cutoff_ + beta_ * std::abs(a.dx);
      double ax = alpha(cutoff, dt);
      a.x = ax * x + (1 - ax) * a.x;
      (c == 0 ? out.k[j].x : out.k[j].y) = float(a.x);
    }
  }
  last_t_ = t;
  return out;
}

// --- resampling ------------------------------------------------------------------------------------

namespace {
Skeleton lerp(const Skeleton& a, const Skeleton& b, float f) {
  if (!a.found() || !b.found()) return f < 0.5f ? a : b;
  Skeleton s;
  for (int j = 0; j < JointCount; ++j) {
    s.k[j] = {a.k[j].x + (b.k[j].x - a.k[j].x) * f, a.k[j].y + (b.k[j].y - a.k[j].y) * f,
              a.k[j].score + (b.k[j].score - a.k[j].score) * f};
  }
  s.score = a.score + (b.score - a.score) * f;
  return s;
}
}  // namespace

void Track::clear() {
  frames_.clear();
  last_.reset();
}

void Track::add(double t, const Skeleton& s) {
  const double step = 1.0 / kRate;
  if (last_ && (t - last_->t > 1.0 || t < last_->t)) clear();
  if (!last_) {
    frames_.push_back({t, s});
    last_ = Frame{t, s};
    return;
  }
  // Emit the samples on the fixed grid between the last input and this one.
  double next = frames_.empty() ? t : frames_.back().t + step;
  while (next <= t + 1e-9) {
    float f = float((next - last_->t) / std::max(1e-9, t - last_->t));
    frames_.push_back({next, lerp(last_->s, s, std::clamp(f, 0.f, 1.f))});
    next += step;
  }
  last_ = Frame{t, s};
  while (frames_.size() > capacity_) frames_.pop_front();
}

// --- body measures ---------------------------------------------------------------------------------

float Body::torso(const Skeleton& s) {
  const auto& k = s.k;
  if (seen(k[LShoulder]) && seen(k[RShoulder]) && seen(k[LHip]) && seen(k[RHip]))
    return dist(mid(k[LShoulder], k[RShoulder]), mid(k[LHip], k[RHip]));
  for (auto [sh, hp] : {std::pair{LShoulder, LHip}, std::pair{RShoulder, RHip}})
    if (seen(k[sh]) && seen(k[hp])) return dist(pt(k[sh]), pt(k[hp]));
  return 0;
}

std::optional<float> Body::hip_height(const Skeleton& s, float torso) {
  const auto& k = s.k;
  if (torso <= 0) return std::nullopt;
  float hy = 0, ay = 0;
  int nh = 0, na = 0;
  for (int j : {LHip, RHip}) if (seen(k[j])) hy += k[j].y, ++nh;
  for (int j : {LAnkle, RAnkle}) if (seen(k[j])) ay += k[j].y, ++na;
  if (!nh || !na) return std::nullopt;
  return (ay / na - hy / nh) / torso;
}

std::optional<float> Body::knee_angle(const Skeleton& s) {
  const auto& k = s.k;
  std::optional<float> best;
  for (auto [h, n, a] : {std::tuple{LHip, LKnee, LAnkle}, std::tuple{RHip, RKnee, RAnkle}}) {
    if (!seen(k[h]) || !seen(k[n]) || !seen(k[a])) continue;
    float v = angle(pt(k[h]), pt(k[n]), pt(k[a]));
    if (!best || v < *best) best = v;
  }
  return best;
}

std::optional<float> Body::elbow_angle(const Skeleton& s) {
  const auto& k = s.k;
  float sum = 0;
  int n = 0;
  for (auto [sh, el, wr] : {std::tuple{LShoulder, LElbow, LWrist}, std::tuple{RShoulder, RElbow, RWrist}}) {
    if (!seen(k[sh]) || !seen(k[el]) || !seen(k[wr])) continue;
    sum += angle(pt(k[sh]), pt(k[el]), pt(k[wr]));
    ++n;
  }
  if (!n) return std::nullopt;
  return sum / n;
}

std::optional<float> Body::hip_angle(const Skeleton& s) {
  const auto& k = s.k;
  float sum = 0;
  int n = 0;
  for (auto [sh, h, kn] : {std::tuple{LShoulder, LHip, LKnee}, std::tuple{RShoulder, RHip, RKnee}}) {
    if (!seen(k[sh]) || !seen(k[h]) || !seen(k[kn])) continue;
    sum += angle(pt(k[sh]), pt(k[h]), pt(k[kn]));
    ++n;
  }
  if (!n) return std::nullopt;
  return sum / n;
}

std::optional<float> Body::wrist_lift(const Skeleton& s, float torso) {
  const auto& k = s.k;
  if (torso <= 0) return std::nullopt;
  float sum = 0;
  int n = 0;
  for (auto [sh, wr] : {std::pair{LShoulder, LWrist}, std::pair{RShoulder, RWrist}}) {
    if (!seen(k[sh]) || !seen(k[wr])) continue;
    sum += (k[sh].y - k[wr].y) / torso;
    ++n;
  }
  if (!n) return std::nullopt;
  return sum / n;
}

std::optional<float> Body::arm_raise(const Skeleton& s) {
  const auto& k = s.k;
  float sum = 0;
  int n = 0;
  for (auto [h, sh, el] : {std::tuple{LHip, LShoulder, LElbow}, std::tuple{RHip, RShoulder, RElbow}}) {
    if (!seen(k[h]) || !seen(k[sh]) || !seen(k[el])) continue;
    sum += angle(pt(k[h]), pt(k[sh]), pt(k[el]));
    ++n;
  }
  if (!n) return std::nullopt;
  return sum / n;
}

std::optional<float> Body::stance(const Skeleton& s, float torso) {
  const auto& k = s.k;
  if (torso <= 0 || !seen(k[LAnkle]) || !seen(k[RAnkle])) return std::nullopt;
  return std::abs(k[LAnkle].x - k[RAnkle].x) / torso;
}

std::optional<float> Body::torso_lean(const Skeleton& s) {
  const auto& k = s.k;
  if (!seen(k[LShoulder]) || !seen(k[RShoulder]) || !seen(k[LHip]) || !seen(k[RHip])) return std::nullopt;
  Point sh = mid(k[LShoulder], k[RShoulder]), hp = mid(k[LHip], k[RHip]);
  return std::atan2(std::abs(sh.x - hp.x), std::max(1e-3f, hp.y - sh.y)) * 180.f / std::numbers::pi_v<float>;
}

// --- files -----------------------------------------------------------------------------------------

std::vector<Frame> read_keypoints(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::vector<Frame> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    Frame f;
    ss >> f.t;
    float total = 0;
    for (auto& k : f.s.k) {
      ss >> k.x >> k.y >> k.score;
      total += k.score;
    }
    if (!ss) throw std::runtime_error("bad line in " + path + ": " + line.substr(0, 60));
    f.s.score = total / float(JointCount);
    out.push_back(f);
  }
  return out;
}

void write_keypoints(const std::string& path, const std::vector<Frame>& frames) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write " + path);
  out << "# spotter keypoints v1: t, then x y score for the 17 COCO joints\n" << std::fixed;
  for (const auto& f : frames) {
    out << std::setprecision(4) << f.t;
    for (const auto& k : f.s.k) out << ' ' << std::setprecision(2) << k.x << ' ' << k.y << ' ' << std::setprecision(3) << k.score;
    out << '\n';
  }
}

}  // namespace spotter
