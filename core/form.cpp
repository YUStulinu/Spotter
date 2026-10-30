#include "form.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace spotter {

View view_of(const Skeleton& s) {
  const auto& k = s.k;
  float torso = Body::torso(s);
  if (torso <= 0 || !seen(k[LShoulder]) || !seen(k[RShoulder])) return View::Unknown;
  float width = dist(pt(k[LShoulder]), pt(k[RShoulder])) / torso;
  if (width > 0.55f) return View::Front;
  if (width < 0.3f) return View::Side;
  return View::Unknown;
}

namespace {

// The frame nearest a time.
const Frame* at(const std::vector<Frame>& frames, double t) {
  const Frame* best = nullptr;
  for (const auto& f : frames)
    if (!best || std::abs(f.t - t) < std::abs(best->t - t)) best = &f;
  return best;
}

// The view most frames agree on.
View view_over(const std::vector<Frame>& frames) {
  int front = 0, side = 0;
  for (const auto& f : frames) {
    View v = view_of(f.s);
    front += v == View::Front;
    side += v == View::Side;
  }
  if (front > int(frames.size()) / 2) return View::Front;
  if (side > int(frames.size()) / 2) return View::Side;
  return View::Unknown;
}

void knees_in(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  // Around the bottom: the narrowest knees against the ankles.
  float worst = 1e9f;
  double when = rep.far;
  for (const auto& f : frames) {
    if (std::abs(f.t - rep.far) > 0.25) continue;
    const auto& k = f.s.k;
    if (!seen(k[LKnee]) || !seen(k[RKnee]) || !seen(k[LAnkle]) || !seen(k[RAnkle])) continue;
    float ankles = std::abs(k[LAnkle].x - k[RAnkle].x), knees = std::abs(k[LKnee].x - k[RKnee].x);
    float torso = Body::torso(f.s);
    if (torso <= 0 || ankles < 0.35f * torso) continue;  // feet together: nothing to compare
    float r = knees / ankles;
    if (r < worst) worst = r, when = f.t;
  }
  if (worst < lim.knee_ratio)
    out.push_back({"knees_in", "Knees out", std::format("knees {:.0f}% of the ankles' width at the bottom", worst * 100), when, worst});
}

void depth(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  // How high the hips stayed above the knees at the lowest point.
  float best = 1e9f;
  double when = rep.far;
  for (const auto& f : frames) {
    const auto& k = f.s.k;
    float torso = Body::torso(f.s);
    if (torso <= 0 || !seen(k[LHip]) || !seen(k[RHip]) || !seen(k[LKnee]) || !seen(k[RKnee])) continue;
    float above = (mid(k[LKnee], k[RKnee]).y - mid(k[LHip], k[RHip]).y) / torso;
    if (above < best) best = above, when = f.t;
  }
  if (best < 1e8f && best > lim.depth)
    out.push_back({"depth", "Go deeper", std::format("hips {:.2f} torso lengths above the knees at the bottom", best), when, best});
}

void hips_first(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  // The first part of the pull, from the bottom: did the hips rise much faster than the shoulders?
  const Frame* bottom = nullptr;
  for (const auto& f : frames)
    if (f.t <= rep.far && (!bottom || f.s.k[LHip].y + f.s.k[RHip].y > bottom->s.k[LHip].y + bottom->s.k[RHip].y)) bottom = &f;
  if (!bottom) return;
  const Frame* early = at(frames, bottom->t + 0.4 * std::max(0.3, rep.far - bottom->t));
  if (!early || early == bottom) return;
  const auto &a = bottom->s.k, &b = early->s.k;
  float hips = mid(a[LHip], a[RHip]).y - mid(b[LHip], b[RHip]).y;
  float shoulders = mid(a[LShoulder], a[RShoulder]).y - mid(b[LShoulder], b[RShoulder]).y;
  float torso = Body::torso(bottom->s);
  if (torso <= 0 || hips < 0.1f * torso) return;  // barely moved
  float ratio = hips / std::max(0.02f * torso, shoulders);
  if (ratio > lim.hips_first)
    out.push_back({"hips_first", "Chest up, push the floor away",
                   std::format("hips rose {:.1f} times faster than the shoulders off the floor", ratio), early->t, ratio});
}

void lockout(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  float best = 0;
  for (const auto& f : frames)
    if (auto a = Body::hip_angle(f.s)) best = std::max(best, *a);
  if (best > 0 && best < lim.lockout)
    out.push_back({"lockout", "Finish the lockout", std::format("hips opened to {:.0f} degrees at the top", best), rep.far, best});
}

void press(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  const Frame* top = at(frames, rep.far);
  if (!top) return;
  const auto& k = top->s.k;
  float torso = Body::torso(top->s);
  if (torso > 0 && seen(k[LWrist]) && seen(k[RWrist])) {
    float diff = std::abs(k[LWrist].y - k[RWrist].y) / torso;
    if (diff > lim.uneven)
      out.push_back({"uneven", "Press evenly", std::format("one hand {:.2f} torso lengths lower at the top", diff), rep.far, diff});
  }
  float elbows = 0;
  for (const auto& f : frames)
    if (std::abs(f.t - rep.far) < 0.3)
      if (auto a = Body::elbow_angle(f.s)) elbows = std::max(elbows, *a);
  if (elbows > 0 && elbows < lim.press_lockout)
    out.push_back({"lockout", "Lock out overhead", std::format("elbows reached {:.0f} degrees", elbows), rep.far, elbows});
}

void swing(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  float lo = 1e9f, hi = -1e9f;
  for (const auto& f : frames)
    if (auto a = Body::torso_lean(f.s)) lo = std::min(lo, *a), hi = std::max(hi, *a);
  if (hi > lo && hi - lo > lim.swing)
    out.push_back({"swing", "Keep your body still", std::format("torso swung {:.0f} degrees", hi - lo), rep.far, hi - lo});
}

void too_high(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  float top = 0;
  for (const auto& f : frames)
    if (auto a = Body::arm_raise(f.s)) top = std::max(top, *a);
  if (top > lim.too_high)
    out.push_back({"too_high", "Stop at shoulder height", std::format("arms raised to {:.0f} degrees", top), rep.far, top});
}

void sag(const Rep& rep, const std::vector<Frame>& frames, const FormLimits& lim, std::vector<Fault>& out) {
  float worst = 180;
  double when = rep.far;
  for (const auto& f : frames) {
    const auto& k = f.s.k;
    // The side facing the camera.
    for (auto [sh, h, an] : {std::tuple{LShoulder, LHip, LAnkle}, std::tuple{RShoulder, RHip, RAnkle}}) {
      if (!seen(k[sh]) || !seen(k[h]) || !seen(k[an])) continue;
      float a = angle(pt(k[sh]), pt(k[h]), pt(k[an]));
      if (a < worst) worst = a, when = f.t;
    }
  }
  if (worst < lim.sag)
    out.push_back({"sag", "Keep your body in a line", std::format("shoulder-hip-ankle bent to {:.0f} degrees", worst), when, worst});
}

}  // namespace

std::vector<Fault> check_form(const Exercise& e, const Rep& rep, const std::vector<Frame>& frames, const FormLimits& limits) {
  std::vector<Fault> out;
  if (frames.size() < 5) return out;
  View view = view_over(frames);
  if (e.id == "squats") {
    if (view == View::Front) knees_in(rep, frames, limits, out);
    depth(rep, frames, limits, out);
  } else if (e.id == "lunges") {
    if (view == View::Front) knees_in(rep, frames, limits, out);
  } else if (e.id == "deadlift") {
    if (view == View::Side) hips_first(rep, frames, limits, out);
    if (view != View::Front) lockout(rep, frames, limits, out);
  } else if (e.id == "dumbbell_shoulder_press") {
    if (view == View::Front) press(rep, frames, limits, out);
  } else if (e.id == "bicep_curls") {
    swing(rep, frames, limits, out);
  } else if (e.id == "lateral_shoulder_raises") {
    swing(rep, frames, limits, out);
    if (view == View::Front) too_high(rep, frames, limits, out);
  } else if (e.id == "pushups") {
    if (view == View::Side) sag(rep, frames, limits, out);
  }
  return out;
}

}  // namespace spotter
