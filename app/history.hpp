#pragma once
// The Plan and Progress pages: the log's sessions and estimated maxima for the charts, and the
// planner (the language model checked by the progression rules) run off the UI thread.
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class History : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QStringList exercises READ exercises NOTIFY changed)
  Q_PROPERTY(QVariantList workouts READ workouts NOTIFY changed)
  Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
  Q_PROPERTY(bool planning READ planning NOTIFY planningChanged)
  Q_PROPERTY(QString planStatus READ planStatus NOTIFY planningChanged)

 public:
  explicit History(QObject* parent = nullptr);
  QStringList exercises() const;
  QVariantList workouts() const;
  QVariantMap plan() const { return plan_; }
  bool planning() const { return planning_; }
  QString planStatus() const { return status_; }

  // Points {t (ms since the epoch), v} of the estimated one-repetition maximum.
  Q_INVOKABLE QVariantList maxima(const QString& exercise) const;
  // Repetitions per session (body-weight exercises have no weight to chart).
  Q_INVOKABLE QVariantList volume(const QString& exercise) const;
  Q_INVOKABLE void makePlan(const QString& goal, int days, const QString& experience, double bodyweight, bool barbell, int fatigue);
  Q_INVOKABLE QString exerciseName(const QString& id) const;

 signals:
  void changed();
  void planChanged();
  void planningChanged();

 private:
  QVariantMap plan_;
  bool planning_ = false;
  QString status_;
};
