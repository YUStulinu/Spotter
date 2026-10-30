#include "session.hpp"

#include <algorithm>

namespace spotter {

Session::Session(Classifier* classifier, SessionOptions options) : classifier_(classifier), opt_(options) {}

void Session::force(const std::string& exercise) { forced_ = exercise; }

void Session::unforce() { forced_.clear(); }

void Session::start(const std::string& id, double t, std::vector<Event>& out) {
  const Exercise* e = find_exercise(id);
  if (!e) return;
  exercise_ = id;
  counter_ = std::make_unique<RepCounter>(*e);
  last_active_ = t;
  out.push_back({Event::SetStarted, t, id, 0, std::nullopt, std::nullopt});
  // The classifier needs a second or two to be sure: count what happened meanwhile.
  for (const auto& f : history_)
    if (f.t >= t - opt_.replay) feed(f, out);
}

void Session::end(double t, std::vector<Event>& out) {
  if (!counter_) return;
  // Repetitions that started after the set ended (putting the weights down) do not count.
  int reps = int(std::count_if(counter_->reps().begin(), counter_->reps().end(), [&](const Rep& r) { return r.start <= t; }));
  // A set the classifier started with fewer than opt_.min_reps repetitions was the lifter moving
  // between sets (sitting down, picking up the weights): it is dropped. A planned set always counts.
  if (forced_.empty() && reps < opt_.min_reps) reps = 0;
  out.push_back({Event::SetEnded, t, exercise_, reps, std::nullopt, std::nullopt});
  counter_.reset();
  exercise_.clear();
  candidate_.clear();
}

void Session::feed(const Frame& f, std::vector<Event>& out) {
  auto rep = counter_->add(f.t, f.s);
  if (!rep) return;
  out.push_back({Event::RepDone, f.t, exercise_, counter_->count(), rep, std::nullopt});
  std::vector<Frame> frames;
  for (const auto& h : history_)
    if (h.t >= rep->start - 0.1 && h.t <= rep->end + 0.1) frames.push_back(h);
  for (auto& fault : check_form(counter_->exercise(), *rep, frames))
    out.push_back({Event::FormFault, f.t, exercise_, counter_->count(), rep, fault});
}

std::vector<Event> Session::add(double t, const Skeleton& raw) {
  std::vector<Event> out;
  smoothed_ = smoother_(t, raw);
  track_.add(t, smoothed_);
  const auto& frames = track_.frames();
  for (auto it = frames.begin(); it != frames.end(); ++it) {
    if (it->t <= last_frame_t_) continue;
    const Frame& f = *it;
    last_frame_t_ = f.t;
    history_.push_back(f);
    while (!history_.empty() && history_.front().t < f.t - 15) history_.pop_front();

    if (!forced_.empty()) {
      if (exercise_ != forced_) {
        end(f.t, out);
        start(forced_, f.t, out);
      } else {
        feed(f, out);
      }
      continue;
    }
    if (counter_) feed(f, out);
    if (!classifier_ || ++since_classify_ < opt_.classify_every) continue;
    since_classify_ = 0;
    prediction_ = classifier_->classify(frames);
    if (prediction_.p.empty()) continue;
    if (avg_.size() != prediction_.p.size()) avg_ = prediction_.p;
    for (size_t c = 0; c < avg_.size(); ++c) avg_[c] += 0.3f * (prediction_.p[c] - avg_[c]);
    size_t best = size_t(std::max_element(avg_.begin(), avg_.end()) - avg_.begin());
    const std::string& name = classifier_->classes()[best];
    bool exercise = find_exercise(name) != nullptr;
    if (counter_ && name == exercise_) last_active_ = f.t;
    if (counter_ && f.t - last_active_ > opt_.end_rest) {
      // Ended at the last moment the exercise was seen: what came after (racking the weights) is not a repetition.
      end(last_active_, out);
    }
    if (exercise && avg_[best] >= opt_.start_confidence && name != exercise_) {
      if (candidate_ != name) {
        candidate_ = name;
        candidate_since_ = f.t;
      } else if (f.t - candidate_since_ >= opt_.start_hold) {
        end(f.t, out);
        start(name, f.t, out);
      }
    } else if (!exercise || name == exercise_) {
      candidate_.clear();
    }
  }
  return out;
}

std::vector<Event> Session::finish() {
  std::vector<Event> out;
  end(last_frame_t_, out);
  return out;
}

}  // namespace spotter
