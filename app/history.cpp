#include "history.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QThread>

#include "coach.hpp"
#include "llm.hpp"
#include "planner.hpp"
#include "reps.hpp"

using namespace spotter;

History::History(QObject* parent) : QObject(parent) {
  connect(Coach::instance(), &Coach::logChanged, this, &History::changed);
  if (auto json = Coach::instance()->log().plan()) {
    plan_ = QJsonDocument::fromJson(json->toUtf8()).object().toVariantMap();
    status_ = "The last plan";
  }
}

QStringList History::exercises() const { return Coach::instance()->log().exercises(); }

QString History::exerciseName(const QString& id) const {
  const Exercise* e = find_exercise(id.toStdString());
  return e ? QString::fromStdString(e->name) : id;
}

QVariantList History::workouts() const {
  QVariantList out;
  for (const auto& w : Coach::instance()->log().workouts(60)) {
    QStringList sets;
    for (const auto& s : w.sets)
      sets << QString("%1 %2%3").arg(exerciseName(s.exercise)).arg(s.reps_done).arg(s.weight_kg > 0 ? QString(" @ %1 kg").arg(s.weight_kg) : QString());
    out << QVariantMap{{"date", w.date.toString("d MMM yyyy")}, {"fatigue", w.fatigue}, {"sets", sets.join(" · ")}};
  }
  return out;
}

QVariantList History::maxima(const QString& exercise) const {
  QVariantList out;
  for (const auto& [date, v] : Coach::instance()->log().estimatedMax(exercise))
    out << QVariantMap{{"t", double(QDateTime(date, QTime(12, 0)).toMSecsSinceEpoch())}, {"v", v}};
  return out;
}

QVariantList History::volume(const QString& exercise) const {
  QVariantList out;
  for (const auto& s : Coach::instance()->log().history(exercise, 1000).sessions) {
    int reps = 0;
    for (const auto& r : s.sets) reps += r.reps_done;
    out << QVariantMap{{"t", double(QDateTime(s.date, QTime(12, 0)).toMSecsSinceEpoch())}, {"v", reps}};
  }
  return out;
}

void History::makePlan(const QString& goal, int days, const QString& experience, double bodyweight, bool barbell, int fatigue) {
  if (planning_) return;
  planning_ = true;
  status_ = "Starting the language model…";
  emit planningChanged();
  Coach::instance()->setFatigue(fatigue);
  Profile p;
  p.goal = goal, p.days = days, p.experience = experience, p.bodyweight_kg = bodyweight;
  p.equipment = barbell ? QStringList{"barbell", "dumbbells"} : QStringList{"dumbbells"};
  std::vector<ExerciseHistory> history;
  for (const auto& e : exercise_loads()) history.push_back(Coach::instance()->log().history(e.id, 4));
  std::optional<Plan> current;
  if (!plan_.isEmpty()) current = Plan::fromJson(QJsonObject::fromVariantMap(plan_));

  QThread* t = QThread::create([this, p, history, fatigue, current] {
    Plan plan;
    QString status;
    try {
      static LlmServer server(LlmServer::default_model());  // kept running for the next plan
      if (!server.start()) throw std::runtime_error("the language model is not installed (scripts/fetch-deps.sh --llm)");
      Llm llm;
      auto reply = llm.chat(kPlanSystem, plan_prompt(p, history, fatigue, current), plan_schema(), 3000);
      QString error;
      auto proposed = Plan::fromJson(QJsonDocument::fromJson(reply.text.toUtf8()).object(), &error);
      if (!proposed) throw std::runtime_error(("the model's plan was unusable: " + error).toStdString());
      Check c = validate(*proposed, p, history, fatigue, current);
      plan = c.plan;
      status = c.adjusted ? QString("Made by the model in %1 s; the rules changed %2 of %3 numbers").arg(reply.ms / 1000, 0, 'f', 0).arg(c.adjusted).arg(c.items)
                          : QString("Made by the model in %1 s, within the rules").arg(reply.ms / 1000, 0, 'f', 0);
    } catch (const std::exception& e) {
      plan = rules_plan(p, history, fatigue, current);
      status = QString("From the rules alone (%1)").arg(e.what());
    }
    QVariantMap map = plan.toJson().toVariantMap();
    QString json = QString::fromUtf8(QJsonDocument(plan.toJson()).toJson(QJsonDocument::Compact));
    QMetaObject::invokeMethod(this, [this, map, json, status] {
      Coach::instance()->log().savePlan(json);
      plan_ = map;
      status_ = status;
      planning_ = false;
      emit planChanged();
      emit planningChanged();
    }, Qt::QueuedConnection);
  });
  connect(t, &QThread::finished, t, &QObject::deleteLater);
  t->start();
}
