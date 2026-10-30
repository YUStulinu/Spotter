#include "log.hpp"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <map>
#include <stdexcept>

namespace spotter {
namespace {

const char* kSchema[] = {
    "PRAGMA foreign_keys = ON",
    "CREATE TABLE IF NOT EXISTS workouts ("
    " id INTEGER PRIMARY KEY, date TEXT NOT NULL, fatigue INTEGER NOT NULL CHECK (fatigue BETWEEN 0 AND 10),"
    " notes TEXT NOT NULL DEFAULT '')",
    "CREATE TABLE IF NOT EXISTS sets ("
    " id INTEGER PRIMARY KEY, workout_id INTEGER NOT NULL REFERENCES workouts(id) ON DELETE CASCADE,"
    " exercise TEXT NOT NULL, set_no INTEGER NOT NULL, reps_planned INTEGER NOT NULL, reps_done INTEGER NOT NULL,"
    " weight_kg REAL NOT NULL CHECK (weight_kg >= 0), rpe REAL NOT NULL DEFAULT 0, faults INTEGER NOT NULL DEFAULT 0,"
    " rep_seconds REAL NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS sets_exercise ON sets(exercise, workout_id)",
    "CREATE TABLE IF NOT EXISTS plans (id INTEGER PRIMARY KEY, created TEXT NOT NULL DEFAULT (datetime('now')), json TEXT NOT NULL)",
};

void exec(QSqlQuery& q) {
  if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

}  // namespace

double epley(double weight, int reps) { return reps <= 0 ? 0 : reps == 1 ? weight : weight * (1 + reps / 30.0); }

WorkoutLog::WorkoutLog(const QString& path) : name_(QUuid::createUuid().toString()) {
  db_ = QSqlDatabase::addDatabase("QSQLITE", name_);
  db_.setDatabaseName(path);
  if (!db_.open()) throw std::runtime_error(db_.lastError().text().toStdString());
  for (const char* s : kSchema) {
    QSqlQuery q(db_);
    q.prepare(s);
    exec(q);
  }
}

WorkoutLog::~WorkoutLog() {
  db_.close();
  db_ = QSqlDatabase();
  QSqlDatabase::removeDatabase(name_);
}

qint64 WorkoutLog::startWorkout(QDate date, int fatigue, const QString& notes) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO workouts(date, fatigue, notes) VALUES (?, ?, ?)");
  q.addBindValue(date.toString(Qt::ISODate));
  q.addBindValue(fatigue);
  q.addBindValue(notes.isNull() ? QString("") : notes);
  exec(q);
  return q.lastInsertId().toLongLong();
}

void WorkoutLog::setFatigue(qint64 workout, int fatigue) {
  QSqlQuery q(db_);
  q.prepare("UPDATE workouts SET fatigue = ? WHERE id = ?");
  q.addBindValue(fatigue);
  q.addBindValue(workout);
  exec(q);
}

void WorkoutLog::rateLastSet(qint64 workout, double rpe) {
  QSqlQuery q(db_);
  q.prepare("UPDATE sets SET rpe = ? WHERE id = (SELECT MAX(id) FROM sets WHERE workout_id = ?)");
  q.addBindValue(rpe);
  q.addBindValue(workout);
  exec(q);
}

void WorkoutLog::addSet(qint64 workout, const SetRecord& s) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO sets(workout_id, exercise, set_no, reps_planned, reps_done, weight_kg, rpe, faults, rep_seconds)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  for (QVariant v : {QVariant(workout), QVariant(s.exercise), QVariant(s.set_no), QVariant(s.reps_planned), QVariant(s.reps_done),
                     QVariant(s.weight_kg), QVariant(s.rpe), QVariant(s.faults), QVariant(s.rep_seconds)})
    q.addBindValue(v);
  exec(q);
}

static SetRecord readSet(const QSqlQuery& q, int from) {
  SetRecord s;
  s.exercise = q.value(from).toString();
  s.set_no = q.value(from + 1).toInt();
  s.reps_planned = q.value(from + 2).toInt();
  s.reps_done = q.value(from + 3).toInt();
  s.weight_kg = q.value(from + 4).toDouble();
  s.rpe = q.value(from + 5).toDouble();
  s.faults = q.value(from + 6).toInt();
  s.rep_seconds = q.value(from + 7).toDouble();
  return s;
}

static const char* kSetColumns = "s.exercise, s.set_no, s.reps_planned, s.reps_done, s.weight_kg, s.rpe, s.faults, s.rep_seconds";

std::vector<Workout> WorkoutLog::workouts(int limit) const {
  std::vector<Workout> out;
  std::map<qint64, size_t> at;
  QSqlQuery q(db_);
  q.prepare("SELECT id, date, fatigue, notes FROM workouts ORDER BY date DESC, id DESC LIMIT ?");
  q.addBindValue(limit);
  exec(q);
  while (q.next()) {
    at[q.value(0).toLongLong()] = out.size();
    out.push_back({q.value(0).toLongLong(), QDate::fromString(q.value(1).toString(), Qt::ISODate), q.value(2).toInt(), q.value(3).toString(), {}});
  }
  QSqlQuery s(db_);
  s.prepare(QString("SELECT s.workout_id, %1 FROM sets s ORDER BY s.workout_id, s.exercise, s.set_no").arg(kSetColumns));
  exec(s);
  while (s.next())
    if (auto it = at.find(s.value(0).toLongLong()); it != at.end()) out[it->second].sets.push_back(readSet(s, 1));
  return out;
}

ExerciseHistory WorkoutLog::history(const QString& exercise, int sessions) const {
  ExerciseHistory h{exercise, {}};
  QSqlQuery q(db_);
  q.prepare(QString("SELECT w.id, w.date, w.fatigue, %1 FROM sets s JOIN workouts w ON w.id = s.workout_id"
                    " WHERE s.exercise = ? AND w.id IN (SELECT w2.id FROM workouts w2 JOIN sets s2 ON s2.workout_id = w2.id"
                    "   WHERE s2.exercise = ? GROUP BY w2.id ORDER BY w2.date DESC, w2.id DESC LIMIT ?)"
                    " ORDER BY w.date, w.id, s.set_no").arg(kSetColumns));
  q.addBindValue(exercise);
  q.addBindValue(exercise);
  q.addBindValue(sessions);
  exec(q);
  qint64 last = -1;
  while (q.next()) {
    if (q.value(0).toLongLong() != last) {
      last = q.value(0).toLongLong();
      h.sessions.push_back({QDate::fromString(q.value(1).toString(), Qt::ISODate), q.value(2).toInt(), {}});
    }
    h.sessions.back().sets.push_back(readSet(q, 3));
  }
  return h;
}

QStringList WorkoutLog::exercises() const {
  QStringList out;
  QSqlQuery q(db_);
  q.prepare("SELECT exercise FROM sets GROUP BY exercise ORDER BY MAX(id) DESC");
  exec(q);
  while (q.next()) out << q.value(0).toString();
  return out;
}

std::vector<std::pair<QDate, double>> WorkoutLog::estimatedMax(const QString& exercise) const {
  std::vector<std::pair<QDate, double>> out;
  for (const auto& s : history(exercise, 1000).sessions) {
    double best = 0;
    for (const auto& set : s.sets) best = std::max(best, epley(set.weight_kg, set.reps_done));
    if (best > 0) out.emplace_back(s.date, best);
  }
  return out;
}

void WorkoutLog::savePlan(const QString& json) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO plans(json) VALUES (?)");
  q.addBindValue(json);
  exec(q);
}

std::optional<QString> WorkoutLog::plan() const {
  QSqlQuery q(db_);
  q.prepare("SELECT json FROM plans ORDER BY id DESC LIMIT 1");
  exec(q);
  if (q.next()) return q.value(0).toString();
  return std::nullopt;
}

}  // namespace spotter
