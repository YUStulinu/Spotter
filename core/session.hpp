#pragma once
// The live coach without a screen: skeletons in, events out. It smooths and resamples the skeleton,
// asks the classifier what is being done a few times a second, starts a set when one exercise holds
// for a moment (replaying the last seconds so the first repetitions count), counts repetitions,
// checks each one's form, and ends the set after a rest.
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "classifier.hpp"
#include "form.hpp"
#include "reps.hpp"

namespace spotter {

struct Event {
  enum Kind { SetStarted, RepDone, FormFault, SetEnded } kind;
  double t = 0;
  std::string exercise;
  int reps = 0;              // so far in the set
  std::optional<Rep> rep;    // RepDone
  std::optional<Fault> fault;
};

struct SessionOptions {
  int classify_every = 5;        // frames at 30 Hz between classifications
  float start_confidence = 0.6f; // averaged probability that starts a set
  double start_hold = 0.8;       // seconds it must hold
  double end_rest = 3.0;         // seconds of rest or absence that end a set
  double replay = 3.0;           // seconds replayed into a new set's counter
  int min_reps = 3;              // fewer, and a set the classifier started is not a set
};

class Session {
 public:
  // classifier may be null when the exercise is always chosen by hand.
  explicit Session(Classifier* classifier, SessionOptions options = {});
  std::vector<Event> add(double t, const Skeleton& raw);
  std::vector<Event> finish();  // end of the video or of training: close an open set

  // A planned set: count this exercise, whatever the classifier thinks (and a set starts at once).
  void force(const std::string& exercise);
  void unforce();

  const Skeleton& skeleton() const { return smoothed_; }
  const std::string& exercise() const { return exercise_; }  // "" between sets
  int reps() const { return counter_ ? counter_->count() : 0; }
  float progress() const { return counter_ ? counter_->progress() : 0; }
  const Prediction& prediction() const { return prediction_; }

 private:
  void start(const std::string& id, double t, std::vector<Event>& out);
  void end(double t, std::vector<Event>& out);
  void feed(const Frame& f, std::vector<Event>& out);

  Classifier* classifier_;
  SessionOptions opt_;
  Smoother smoother_;
  Track track_{64 * 6};
  Skeleton smoothed_;
  Prediction prediction_;
  std::vector<float> avg_;       // probabilities, averaged over about a second
  std::string candidate_;
  double candidate_since_ = 0;
  std::string exercise_, forced_;
  std::unique_ptr<RepCounter> counter_;
  double last_active_ = 0;       // the last time the set's exercise was seen
  size_t fed_ = 0;               // frames of the track handed to the counter
  double last_frame_t_ = -1;
  int since_classify_ = 0;
  std::deque<Frame> history_;    // the set's frames, for the form checks
};

}  // namespace spotter
