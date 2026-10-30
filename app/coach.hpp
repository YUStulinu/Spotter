#pragma once
// The live coach behind the Train page: a worker thread reads the camera (or a video, at its own
// pace), finds the skeleton, runs the session and hands each frame and what happened to the UI.
// Repetitions are spoken as they are counted, form cues once per repetition at most, and every
// finished set goes into the workout log.
#include <QDate>
#include <QImage>
#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>
#include <thread>

#include "log.hpp"
#include "speech.hpp"

class Coach : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString exercise READ exercise NOTIFY stateChanged)
  Q_PROPERTY(int reps READ reps NOTIFY stateChanged)
  Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
  Q_PROPERTY(double confidence READ confidence NOTIFY stateChanged)
  Q_PROPERTY(QString cue READ cue NOTIFY cueChanged)
  Q_PROPERTY(QVariantList keypoints READ keypoints NOTIFY stateChanged)
  Q_PROPERTY(QSize frameSize READ frameSize NOTIFY stateChanged)
  Q_PROPERTY(double fps READ fps NOTIFY stateChanged)
  Q_PROPERTY(QString device READ device NOTIFY statusChanged)
  Q_PROPERTY(QVariantList sets READ sets NOTIFY setsChanged)
  Q_PROPERTY(QString planned READ planned NOTIFY plannedChanged)
  Q_PROPERTY(bool voice READ voice WRITE setVoice NOTIFY voiceChanged)
  Q_PROPERTY(QStringList exercises READ exercises CONSTANT)

 public:
  // No default constructor: QML must use create(), the one main() made.
  explicit Coach(const QString& database, QObject* parent = nullptr);
  ~Coach() override;
  static Coach* instance();
  // QML's singleton: the one main() made.
  static Coach* create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
  }

  bool running() const { return running_; }
  QString status() const { return status_; }
  QString exercise() const { return exercise_; }
  int reps() const { return reps_; }
  double progress() const { return progress_; }
  double confidence() const { return confidence_; }
  QString cue() const { return cue_; }
  QVariantList keypoints() const { return keypoints_; }
  QSize frameSize() const { return frame_.size(); }
  double fps() const { return fps_; }
  QString device() const { return device_; }
  QVariantList sets() const { return sets_; }
  QString planned() const { return planned_; }
  bool voice() const { return speech_.enabled(); }
  void setVoice(bool on);
  QStringList exercises() const;
  QImage frame() const { return frame_; }

  Q_INVOKABLE void startCamera(int index = 0);
  Q_INVOKABLE void openVideo(const QUrl& file, bool realtime = true, double from_s = 0);
  Q_INVOKABLE void stop();
  // A set from the plan: count this exercise (whatever the classifier thinks) with these numbers.
  Q_INVOKABLE void planSet(const QString& exercise, int reps, double weight);
  Q_INVOKABLE void clearPlan();
  // After the workout: how tired the lifter feels and how hard the last set was.
  Q_INVOKABLE void rateLastSet(double rpe);
  Q_INVOKABLE void setFatigue(int fatigue);
  // The date a replayed recording is logged under (today by default).
  void setDate(QDate date) { date_ = date; }

  spotter::WorkoutLog& log() { return *log_; }

 signals:
  void runningChanged();
  void statusChanged();
  void stateChanged();
  void cueChanged();
  void setsChanged();
  void plannedChanged();
  void voiceChanged();
  void frameReady();
  void logChanged();

 private:
  void start(std::function<void()> loop);
  void worker(const QString& source, int camera, bool realtime, double from_s);
  void onSetEnded(const QString& exercise, int reps, int faults, double rep_seconds);

  std::unique_ptr<spotter::WorkoutLog> log_;
  spotter::Speech speech_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  bool running_ = false;
  QString status_ = "Choose a camera or a video", device_;
  QString exercise_, cue_, planned_;
  int reps_ = 0;
  double progress_ = 0, confidence_ = 0, fps_ = 0;
  QVariantList keypoints_, sets_;
  QImage frame_;
  QString planned_exercise_;
  int planned_reps_ = 0;
  double planned_weight_ = 0;
  qint64 workout_ = 0;
  int fatigue_ = 0;
  QDate date_ = QDate::currentDate();
};
