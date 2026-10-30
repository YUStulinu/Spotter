#pragma once
// The workout log (SQLite): workouts with the fatigue the lifter reported, and every set with what
// was planned, what was done (reps counted by the camera), the effort felt (RPE) and the form
// faults seen.
#include <QDate>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace spotter {

struct SetRecord {
  QString exercise;
  int set_no = 1;
  int reps_planned = 0;
  int reps_done = 0;
  double weight_kg = 0;
  double rpe = 0;          // 0 when not reported
  int faults = 0;          // reps with a form fault
  double rep_seconds = 0;  // mean time per rep
};

struct Workout {
  qint64 id = 0;
  QDate date;
  int fatigue = 0;  // 1 (fresh) .. 10 (exhausted), reported before training
  QString notes;
  std::vector<SetRecord> sets;
};

// One exercise's sessions, newest last: what the planner looks at.
struct ExerciseHistory {
  QString exercise;
  struct Session {
    QDate date;
    int fatigue = 0;
    std::vector<SetRecord> sets;
  };
  std::vector<Session> sessions;
};

class WorkoutLog {
 public:
  explicit WorkoutLog(const QString& path);  // ":memory:" for tests
  ~WorkoutLog();
  WorkoutLog(const WorkoutLog&) = delete;
  WorkoutLog& operator=(const WorkoutLog&) = delete;

  qint64 startWorkout(QDate date, int fatigue, const QString& notes = {});
  void addSet(qint64 workout, const SetRecord& s);
  void setFatigue(qint64 workout, int fatigue);
  void rateLastSet(qint64 workout, double rpe);  // the effort the lifter felt on the newest set
  std::vector<Workout> workouts(int limit = 1000) const;  // newest first
  ExerciseHistory history(const QString& exercise, int sessions = 8) const;
  QStringList exercises() const;
  // Best estimated one-repetition maximum per session (Epley), for the progress chart.
  std::vector<std::pair<QDate, double>> estimatedMax(const QString& exercise) const;

  // The program the planner made last, as JSON.
  void savePlan(const QString& json);
  std::optional<QString> plan() const;

 private:
  QSqlDatabase db_;
  QString name_;
};

double epley(double weight, int reps);

}  // namespace spotter
