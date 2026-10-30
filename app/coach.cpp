#include "coach.hpp"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "classifier.hpp"
#include "pose.hpp"
#include "reps.hpp"
#include "session.hpp"

using namespace spotter;

namespace {
Coach* g_coach = nullptr;
std::mutex g_plan_mutex;
std::string g_plan;  // the exercise the worker must count ("" = let the classifier decide)
bool g_plan_changed = false;

// What the UI needs to know about the set in progress.
struct SetState {
  std::set<int> faulty;
  double rep_seconds = 0;
  int reps = 0;
};
SetState g_set;
}  // namespace

Coach::Coach(const QString& database, QObject* parent) : QObject(parent) {
  g_coach = this;
  log_ = std::make_unique<WorkoutLog>(database);
}

Coach::~Coach() {
  stop();
  g_coach = nullptr;
}

Coach* Coach::instance() { return g_coach; }

void Coach::setVoice(bool on) {
  speech_.setEnabled(on);
  emit voiceChanged();
}

QStringList Coach::exercises() const {
  QStringList out;
  for (const auto& e : spotter::exercises()) out << QString::fromStdString(e.id);
  return out;
}

void Coach::startCamera(int index) {
  start([this, index] { worker({}, index, true, 0); });
}

void Coach::openVideo(const QUrl& file, bool realtime, double from_s) {
  QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
  start([this, path, realtime, from_s] { worker(path, -1, realtime, from_s); });
}

void Coach::start(std::function<void()> loop) {
  stop();
  stop_ = false;
  running_ = true;
  emit runningChanged();
  thread_ = std::thread(std::move(loop));
}

void Coach::stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
  if (running_) {
    running_ = false;
    emit runningChanged();
  }
}

void Coach::planSet(const QString& exercise, int reps, double weight) {
  planned_exercise_ = exercise;
  planned_reps_ = reps;
  planned_weight_ = weight;
  const Exercise* e = find_exercise(exercise.toStdString());
  planned_ = e ? QString("%1 × %2%3").arg(QString::fromStdString(e->name)).arg(reps).arg(weight > 0 ? QString(" @ %1 kg").arg(weight) : QString()) : QString();
  {
    std::lock_guard lock(g_plan_mutex);
    g_plan = exercise.toStdString();
    g_plan_changed = true;
  }
  emit plannedChanged();
}

void Coach::clearPlan() {
  planned_exercise_.clear();
  planned_.clear();
  {
    std::lock_guard lock(g_plan_mutex);
    g_plan.clear();
    g_plan_changed = true;
  }
  emit plannedChanged();
}

void Coach::setFatigue(int fatigue) {
  fatigue_ = fatigue;
  if (workout_) log_->setFatigue(workout_, fatigue);
}

void Coach::rateLastSet(double rpe) {
  if (sets_.isEmpty()) return;
  auto last = sets_.last().toMap();
  last["rpe"] = rpe;
  sets_.last() = last;
  if (workout_) log_->rateLastSet(workout_, rpe);
  emit setsChanged();
  emit logChanged();
}

void Coach::onSetEnded(const QString& exercise, int reps, int faults, double rep_seconds) {
  if (reps <= 0) return;
  if (!workout_) workout_ = log_->startWorkout(date_, fatigue_);
  bool planned = exercise == planned_exercise_;
  int set_no = 1;
  for (const auto& s : sets_)
    if (s.toMap()["exercise"].toString() == exercise) ++set_no;
  SetRecord r{exercise, set_no, planned ? planned_reps_ : reps, reps, planned ? planned_weight_ : 0, 0, faults, rep_seconds};
  log_->addSet(workout_, r);
  const Exercise* e = find_exercise(exercise.toStdString());
  sets_.append(QVariantMap{{"exercise", exercise}, {"name", e ? QString::fromStdString(e->name) : exercise}, {"set", set_no},
                           {"reps", reps}, {"planned", r.reps_planned}, {"weight", r.weight_kg}, {"faults", faults},
                           {"seconds", rep_seconds}});
  emit setsChanged();
  emit logChanged();
  speech_.say(QString("Set done. %1 reps.").arg(spoken_number(reps)), false);
}

void Coach::worker(const QString& source, int camera, bool realtime, double from_s) {
  auto post = [this](auto fn) { QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection); };
  auto say_status = [&](const QString& s) { post([this, s] { status_ = s; emit statusChanged(); }); };
  try {
    QString root = SPOTTER_ROOT;
    PoseEstimator pose(QDir(root).filePath("third_party/models/pose").toStdString());
    std::unique_ptr<Classifier> classifier;
    QString model = QDir(root).filePath("models/exercise.onnx");
    if (QFile::exists(model)) classifier = std::make_unique<Classifier>(model.toStdString());
    Session session(classifier.get());
    QString dev = QString::fromStdString(pose.device());
    post([this, dev] { device_ = dev; emit statusChanged(); });

    cv::VideoCapture cap;
    if (camera >= 0) cap.open(camera);
    else cap.open(source.toStdString());
    if (!cap.isOpened()) throw std::runtime_error(camera >= 0 ? "no camera found" : "cannot open the video");
    double video_fps = camera >= 0 ? 0 : cap.get(cv::CAP_PROP_FPS);
    if (camera < 0 && video_fps <= 1) video_fps = 30;
    if (camera < 0 && from_s > 0) cap.set(cv::CAP_PROP_POS_FRAMES, std::floor(from_s * video_fps));
    say_status(camera >= 0 ? "Camera on: start moving" : "Playing " + QFileInfo(source).fileName());

    auto begin = std::chrono::steady_clock::now();
    auto last = begin;
    long n = 0;
    double fps = 0;
    cv::Mat img, small;
    while (!stop_ && cap.read(img)) {
      double t = camera >= 0 ? std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() : n / video_fps;
      ++n;
      if (camera < 0 && realtime) {
        auto due = begin + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(t));
        std::this_thread::sleep_until(due);
      }
      {
        std::lock_guard lock(g_plan_mutex);
        if (g_plan_changed) {
          if (g_plan.empty()) session.unforce(); else session.force(g_plan);
          g_plan_changed = false;
        }
      }
      Skeleton s = pose.run(img);
      auto events = session.add(t, s);
      auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      if (dt > 0) fps = fps == 0 ? 1 / dt : 0.9 * fps + 0.1 / dt;

      // The picture for the screen, at most 960 px wide.
      double scale = std::min(1.0, 960.0 / img.cols);
      cv::resize(img, small, cv::Size(), scale, scale, cv::INTER_AREA);
      cv::cvtColor(small, small, cv::COLOR_BGR2RGB);
      QImage frame(small.data, small.cols, small.rows, int(small.step), QImage::Format_RGB888);
      frame = frame.copy();
      QVariantList kps;
      const Skeleton& sm = session.skeleton();
      if (sm.found())
        for (const auto& k : sm.k) kps << QVariant(QVariantList{k.x * scale, k.y * scale, k.score});
      QString ex = QString::fromStdString(session.exercise());
      int reps = session.reps();
      double progress = session.progress(), conf = session.prediction().confidence();
      post([this, frame, kps, ex, reps, progress, conf, fps] {
        frame_ = frame;
        keypoints_ = kps;
        exercise_ = ex;
        reps_ = reps;
        progress_ = progress;
        confidence_ = conf;
        fps_ = fps;
        emit stateChanged();
        emit frameReady();
      });
      for (const auto& e : events) {
        QString ename = QString::fromStdString(e.exercise);
        switch (e.kind) {
          case Event::SetStarted: {
            const Exercise* x = find_exercise(e.exercise);
            QString name = x ? QString::fromStdString(x->name) : ename;
            post([this, name] {
              g_set = {};
              cue_.clear();
              emit cueChanged();
              speech_.say(name);
            });
            break;
          }
          case Event::RepDone: {
            int count = e.reps;
            double secs = e.rep ? e.rep->end - e.rep->start : 0;
            post([this, count, secs] {
              g_set.reps = count;
              g_set.rep_seconds += secs;
              speech_.say(spoken_number(count));
            });
            break;
          }
          case Event::FormFault: {
            QString cue = QString::fromStdString(e.fault->cue), detail = QString::fromStdString(e.fault->detail);
            int rep = e.reps;
            post([this, cue, detail, rep] {
              bool first = g_set.faulty.insert(rep).second;
              cue_ = QString("%1 — rep %2: %3").arg(cue).arg(rep).arg(detail);
              emit cueChanged();
              if (first) speech_.say(cue, false);
            });
            break;
          }
          case Event::SetEnded: {
            int count = e.reps;
            post([this, ename, count] {
              onSetEnded(ename, count, int(g_set.faulty.size()), g_set.reps ? g_set.rep_seconds / g_set.reps : 0);
            });
            break;
          }
        }
      }
    }
    auto tail = session.finish();
    for (const auto& e : tail)
      if (e.kind == Event::SetEnded) {
        QString ename = QString::fromStdString(e.exercise);
        int count = e.reps;
        post([this, ename, count] { onSetEnded(ename, count, int(g_set.faulty.size()), g_set.reps ? g_set.rep_seconds / g_set.reps : 0); });
      }
    say_status(stop_ ? "Stopped" : "Video finished");
  } catch (const std::exception& e) {
    say_status(QString("Error: %1").arg(e.what()));
  }
  post([this] {
    running_ = false;
    emit runningChanged();
  });
}
