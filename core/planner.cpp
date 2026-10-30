#include "planner.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <map>

namespace spotter {

const std::vector<ExerciseLoad>& exercise_loads() {
  static const std::vector<ExerciseLoad> all = {
      {"squats", Load::Barbell, 5, 1.2},
      {"deadlift", Load::Barbell, 5, 1.5},
      {"lunges", Load::Dumbbell, 2, 0.25},
      {"dumbbell_shoulder_press", Load::Dumbbell, 2, 0.2},
      {"dumbbell_rows", Load::Dumbbell, 2, 0.3},
      {"bicep_curls", Load::Dumbbell, 1, 0.15},
      {"tricep_extensions", Load::Dumbbell, 1, 0.15},
      {"lateral_shoulder_raises", Load::Dumbbell, 1, 0.1},
      {"pushups", Load::Bodyweight, 0, 0},
      {"situps", Load::Bodyweight, 0, 0},
      {"jumping_jacks", Load::Bodyweight, 0, 0},
  };
  return all;
}

const ExerciseLoad* find_load(const QString& id) {
  for (const auto& e : exercise_loads())
    if (e.id == id) return &e;
  return nullptr;
}

const char* decision_name(Decision d) {
  static const char* names[] = {"start", "increase", "hold", "deload", "reduce"};
  return names[int(d)];
}

namespace {

double unit(const ExerciseLoad& e) { return e.load == Load::Barbell ? 2.5 : 1.0; }
double snap_down(double w, double u) { return std::floor(w / u + 1e-6) * u; }
double snap(double w, double u) { return std::round(w / u) * u; }

bool completed(const ExerciseHistory::Session& s) {
  return !s.sets.empty() && std::all_of(s.sets.begin(), s.sets.end(), [](const SetRecord& r) { return r.reps_done >= r.reps_planned; });
}

struct Last {
  int sets = 0, reps = 0;
  double weight = 0;
};

Last last_of(const ExerciseHistory::Session& s) {
  Last l;
  l.sets = int(s.sets.size());
  for (const auto& r : s.sets) l.reps = std::max(l.reps, r.reps_planned), l.weight = std::max(l.weight, r.weight_kg);
  return l;
}

}  // namespace

Advice advise(const ExerciseHistory& h, int fatigue, const PlanItem& planned) {
  const ExerciseLoad* e = find_load(h.exercise);
  Advice a;
  if (!e || h.sessions.empty()) {
    a = {Decision::Start, planned.sets, planned.reps, planned.weight_kg, "first session: start light and learn the movement"};
    // Nothing logged to take a set off: a tired first session keeps to two sets.
    if (fatigue >= 8) a.sets = std::min(a.sets, 2), a.decision = Decision::Reduce, a.reason = "very tired today: two sets at most";
    return a;
  }
  const auto& last = h.sessions.back();
  Last l = last_of(last);
  a.sets = l.sets, a.reps = l.reps, a.weight_kg = l.weight;
  bool body = e->load == Load::Bodyweight;
  int reps_done = 0, faults = 0;
  double rpe = 0;
  int rated = 0;
  for (const auto& r : last.sets) {
    reps_done += r.reps_done, faults += r.faults;
    if (r.rpe > 0) rpe += r.rpe, ++rated;
  }
  bool hard = rated > 0 && rpe / rated >= 9;
  bool faulty = reps_done > 0 && double(faults) / reps_done >= 0.3;
  bool missed_twice = h.sessions.size() >= 2 && !completed(last) && !completed(h.sessions[h.sessions.size() - 2]);
  if (fatigue >= 8) {
    a.decision = Decision::Reduce;
    a.sets = std::max(1, l.sets - 1);
    if (fatigue >= 9) {
      if (body) a.reps = std::max(1, l.reps - 2);
      else a.weight_kg = snap_down(l.weight * 0.9, unit(*e));
      a.reason = QString("fatigue %1/10: one set fewer and lighter").arg(fatigue);
    } else {
      a.reason = QString("fatigue %1/10: one set fewer").arg(fatigue);
    }
  } else if (missed_twice) {
    a.decision = Decision::Deload;
    if (body) a.reps = std::max(1, l.reps - 2);
    else a.weight_kg = snap_down(l.weight * 0.9, unit(*e));
    a.reason = "missed repetitions two sessions in a row: 10% lighter to build back up";
  } else if (!completed(last)) {
    a.decision = Decision::Hold;
    a.reason = "missed repetitions last time: same weight, finish every set first";
  } else if (hard) {
    a.decision = Decision::Hold;
    a.reason = QString("last session felt very hard (RPE %1): same weight").arg(rpe / rated, 0, 'f', 1);
  } else if (faulty) {
    a.decision = Decision::Hold;
    a.reason = QString("form slipped on %1 of %2 repetitions: same weight until it is clean").arg(faults).arg(reps_done);
  } else {
    a.decision = Decision::Increase;
    if (body) {
      a.reps = std::min(20, l.reps + 1);
      if (a.reps == l.reps) a.decision = Decision::Hold;
      a.reason = a.decision == Decision::Increase ? "every set done cleanly: one more repetition" : "at 20 repetitions: hold";
    } else {
      a.weight_kg = l.weight + e->step_kg;
      a.reason = QString("every set done cleanly: +%1 kg").arg(e->step_kg);
    }
  }
  return a;
}

int direction(const PlanItem& item, const ExerciseHistory& h) {
  if (h.sessions.empty()) return 0;
  Last l = last_of(h.sessions.back());
  auto cmp = [](double a, double b) { return a > b + 1e-6 ? 1 : a < b - 1e-6 ? -1 : 0; };
  if (int c = cmp(item.weight_kg, l.weight)) return c;
  if (int c = cmp(item.reps, l.reps)) return c;
  return cmp(item.sets, l.sets);
}

// --- JSON ------------------------------------------------------------------------------------------

QJsonObject Plan::toJson() const {
  QJsonArray ds;
  for (const auto& d : days) {
    QJsonArray items;
    for (const auto& i : d.items) {
      QJsonObject o{{"exercise", i.exercise}, {"sets", i.sets}, {"reps", i.reps}, {"weight_kg", i.weight_kg}, {"rest_s", i.rest_s}, {"why", i.why}};
      if (!i.adjusted.isEmpty()) o["adjusted"] = i.adjusted;
      if (!i.decision.isEmpty()) o["decision"] = i.decision;
      items.append(o);
    }
    ds.append(QJsonObject{{"name", d.name}, {"exercises", items}});
  }
  return {{"summary", summary}, {"days", ds}, {"source", source}};
}

std::optional<Plan> Plan::fromJson(const QJsonObject& o, QString* error) {
  Plan p;
  p.summary = o["summary"].toString();
  p.source = o["source"].toString("model");
  if (!o["days"].isArray()) {
    if (error) *error = "no days";
    return std::nullopt;
  }
  for (const auto& dv : o["days"].toArray()) {
    auto d = dv.toObject();
    PlanDay day{d["name"].toString(), {}};
    for (const auto& iv : d["exercises"].toArray()) {
      auto i = iv.toObject();
      day.items.push_back({i["exercise"].toString(), i["sets"].toInt(), i["reps"].toInt(), i["weight_kg"].toDouble(),
                           i["rest_s"].toInt(90), i["why"].toString(), i["adjusted"].toString(), i["decision"].toString()});
    }
    p.days.push_back(day);
  }
  if (p.days.empty()) {
    if (error) *error = "no days";
    return std::nullopt;
  }
  return p;
}

QJsonObject plan_schema() {
  QJsonArray ids;
  for (const auto& e : exercise_loads()) ids.append(e.id);
  QJsonObject item{{"type", "object"},
                   {"properties", QJsonObject{{"exercise", QJsonObject{{"type", "string"}, {"enum", ids}}},
                                              {"decision", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"start", "increase", "hold", "deload", "reduce"}}}},
                                              {"sets", QJsonObject{{"type", "integer"}}},
                                              {"reps", QJsonObject{{"type", "integer"}}},
                                              {"weight_kg", QJsonObject{{"type", "number"}}},
                                              {"rest_s", QJsonObject{{"type", "integer"}}},
                                              {"why", QJsonObject{{"type", "string"}}}}},
                   {"required", QJsonArray{"exercise", "decision", "sets", "reps", "weight_kg", "rest_s", "why"}}};
  QJsonObject day{{"type", "object"},
                  {"properties", QJsonObject{{"name", QJsonObject{{"type", "string"}}},
                                             {"exercises", QJsonObject{{"type", "array"}, {"items", item}, {"minItems", 1}}}}},
                  {"required", QJsonArray{"name", "exercises"}}};
  return {{"type", "object"},
          {"properties", QJsonObject{{"summary", QJsonObject{{"type", "string"}}},
                                     {"days", QJsonObject{{"type", "array"}, {"items", day}, {"minItems", 1}}}}},
          {"required", QJsonArray{"summary", "days"}}};
}

// --- the prompt ------------------------------------------------------------------------------------

const char* kPlanSystem =
    "You are a careful strength coach. You write training plans as JSON and nothing else. "
    "Use progressive overload: add a little weight (or one repetition for body-weight exercises) only when the lifter "
    "completed every set at the last session with clean form and without grinding (RPE below 9). Keep the same weight "
    "when they missed repetitions, when it felt very hard, or when the camera saw form faults on many repetitions. "
    "After two sessions in a row with missed repetitions, take about 10% off. When they report high fatigue (8 or more "
    "out of 10), do one set fewer, and at 9 or 10 also go about 10% lighter. Never jump more than one usual step at a "
    "time. For each exercise, first decide (\"decision\": start for a new exercise, increase, hold, deload or reduce) from that exercise's own log and today's fatigue, then write numbers that follow the decision. Body-weight exercises have weight_kg 0. Dumbbell weights are per dumbbell. In \"why\", say in one short "
    "sentence (at most 15 words) why this number, referring to that exercise's log.";

namespace {

// One exercise's log, with what a coach reads off it spelled out: whether every set was completed,
// how hard it felt, how many repetitions the camera saw form faults on.
QString describe(const ExerciseHistory& h) {
  QStringList lines;
  int missed_in_a_row = 0;
  for (const auto& s : h.sessions) {
    if (s.sets.empty()) continue;
    QStringList done;
    int faults = 0, reps = 0, short_by = 0;
    double rpe = 0;
    int rated = 0;
    for (const auto& r : s.sets) {
      done << QString::number(r.reps_done);
      faults += r.faults, reps += r.reps_done, short_by += std::max(0, r.reps_planned - r.reps_done);
      if (r.rpe > 0) rpe += r.rpe, ++rated;
    }
    missed_in_a_row = short_by > 0 ? missed_in_a_row + 1 : 0;
    Last l = last_of(s);
    QString line = QString("  %1: %2 x %3 @ %4 kg, done %5 (%6)")
                       .arg(s.date.toString(Qt::ISODate)).arg(l.sets).arg(l.reps).arg(l.weight).arg(done.join("/"),
                            short_by ? QString("missed %1 repetitions").arg(short_by) : QString("every set completed"));
    if (rated) line += QString(", RPE %1%2").arg(rpe / rated, 0, 'f', 1).arg(rpe / rated >= 9 ? " (very hard)" : "");
    line += faults ? QString(", form faults on %1 of %2 repetitions%3").arg(faults).arg(reps).arg(reps && double(faults) / reps >= 0.3 ? " (many)" : "")
                   : QString(", clean form");
    lines << line;
  }
  QString out = QString("%1:\n%2").arg(h.exercise, lines.join("\n"));
  if (missed_in_a_row >= 2) out += "\n  (missed repetitions two sessions in a row)";
  return out;
}

}  // namespace

QString plan_prompt(const Profile& p, const std::vector<ExerciseHistory>& history, int fatigue, const std::optional<Plan>& current) {
  QStringList ex;
  for (const auto& e : exercise_loads()) {
    QString kind = e.load == Load::Barbell ? "barbell" : e.load == Load::Dumbbell ? "dumbbells" : "body weight";
    ex << QString("%1 (%2%3)").arg(e.id, kind, e.step_kg > 0 ? QString(", usual step %1 kg").arg(e.step_kg) : QString());
  }
  QString out = QString("Lifter: goal %1, %2, %3 kg, trains %4 days a week, has %5.%6\n\n")
                    .arg(p.goal, p.experience).arg(p.bodyweight_kg).arg(p.days).arg(p.equipment.join(", "),
                                                                                    p.notes.isEmpty() ? QString() : " " + p.notes);
  out += "Exercises the camera can follow (use only these ids): " + ex.join("; ") + ".\n\n";
  if (current) out += "Current plan:\n" + QString::fromUtf8(QJsonDocument(current->toJson()).toJson(QJsonDocument::Compact)) + "\n\n";
  QStringList logs;
  for (const auto& h : history)
    if (!h.sessions.empty()) logs << describe(h);
  out += logs.isEmpty() ? "No workouts logged yet: this is the first plan, so choose conservative starting weights.\n\n"
                        : "Workout log, oldest first:\n" + logs.join("\n") + "\n\n";
  out += QString("Fatigue reported today: %1/10%2.\n\n").arg(fatigue).arg(fatigue >= 8 ? " (high)" : "");
  out += QString("Write the plan for the next %1 training days%2.")
             .arg(p.days)
             .arg(current ? ", keeping the current plan's exercises and adjusting their numbers from the log" : "");
  return out;
}

// --- checking the model's plan ---------------------------------------------------------------------

namespace {

const ExerciseHistory* history_of(const std::vector<ExerciseHistory>& history, const QString& id) {
  for (const auto& h : history)
    if (h.exercise == id) return &h;
  return nullptr;
}

double start_weight(const ExerciseLoad& e, const Profile& p) {
  if (e.load == Load::Bodyweight) return 0;
  double share = p.experience == "beginner" ? 0.4 : 0.6;
  return std::max(e.load == Load::Barbell ? 20.0 : 2.0, snap_down(p.bodyweight_kg * e.max_bodyweight_ratio * share, unit(e)));
}

// Is the model's item within what the rules allow? If not, why.
QString out_of_bounds(const PlanItem& item, const Advice& a, const ExerciseHistory* h, const ExerciseLoad& e, const Profile& p) {
  bool body = e.load == Load::Bodyweight;
  if (body && item.weight_kg != 0) return "body-weight exercise with a weight";
  if (!h || h->sessions.empty()) {
    double ceiling = p.bodyweight_kg * e.max_bodyweight_ratio * (p.experience == "beginner" ? 0.6 : 1.0);
    if (!body && item.weight_kg > ceiling + 1e-6) return QString("%1 kg is heavy for a first session (at most %2)").arg(item.weight_kg).arg(ceiling, 0, 'f', 1);
    if (a.decision == Decision::Reduce && item.sets > a.sets) return "too many sets for the fatigue reported";
    return {};
  }
  Last l = last_of(h->sessions.back());
  const double tol = 1e-6;
  switch (a.decision) {
    case Decision::Increase:
      if (!body && item.weight_kg > l.weight + e.step_kg + tol) return QString("+%1 kg is more than one step").arg(item.weight_kg - l.weight);
      if (body && item.reps > l.reps + 2) return "more than two repetitions added";
      if (item.sets > l.sets + 1) return "more than one set added";
      return {};
    case Decision::Hold:
      if (item.weight_kg > l.weight + tol) return QString("heavier although %1").arg(a.reason);
      if (body && item.reps > l.reps) return QString("more repetitions although %1").arg(a.reason);
      if (item.sets > l.sets) return "more sets although the last session was not a clean success";
      return {};
    case Decision::Deload:
      if (!body && item.weight_kg > l.weight * 0.95 + tol) return "no deload after two sessions with missed repetitions";
      if (body && item.reps >= l.reps) return "no deload after two sessions with missed repetitions";
      if (item.sets > l.sets) return "more sets during a deload";
      return {};
    case Decision::Reduce:
      if (item.sets > a.sets) return QString("%1 sets despite fatigue (at most %2)").arg(item.sets).arg(a.sets);
      if (!body && item.weight_kg > a.weight_kg + tol) return "heavier despite fatigue";
      if (body && item.reps > a.reps) return "more repetitions despite fatigue";
      return {};
    case Decision::Start:
      return {};
  }
  return {};
}

}  // namespace

Check validate(Plan proposed, const Profile& p, const std::vector<ExerciseHistory>& history, int fatigue, const std::optional<Plan>& current) {
  Check c;
  (void)current;
  if (int(proposed.days.size()) > p.days) {
    c.problems << QString("%1 days instead of %2").arg(proposed.days.size()).arg(p.days);
    proposed.days.resize(size_t(p.days));
  }
  for (auto& day : proposed.days) {
    std::vector<PlanItem> kept;
    for (auto item : day.items) {
      const ExerciseLoad* e = find_load(item.exercise);
      if (!e) {
        c.problems << "unknown exercise " + item.exercise;
        continue;
      }
      ++c.items;
      QStringList fixed;
      if (item.sets < 1 || item.sets > 6) fixed << QString("%1 sets").arg(item.sets), item.sets = std::clamp(item.sets, 1, 6);
      if (item.reps < 1 || item.reps > 30) fixed << QString("%1 reps").arg(item.reps), item.reps = std::clamp(item.reps, 1, 30);
      if (item.rest_s < 30 || item.rest_s > 300) item.rest_s = std::clamp(item.rest_s, 30, 300);
      const ExerciseHistory* h = history_of(history, item.exercise);
      ExerciseHistory empty{item.exercise, {}};
      Advice a = advise(h ? *h : empty, fatigue, item);
      QString why = out_of_bounds(item, a, h, *e, p);
      if (!why.isEmpty() || !fixed.isEmpty()) {
        ++c.adjusted;
        QString what = why.isEmpty() ? "out of range: " + fixed.join(", ") : why;
        c.problems << item.exercise + ": " + what;
        if (h && !h->sessions.empty()) {
          item.sets = a.sets, item.reps = a.reps, item.weight_kg = a.weight_kg;
        } else {
          if (e->load == Load::Bodyweight) item.weight_kg = 0;
          else item.weight_kg = std::min(item.weight_kg, start_weight(*e, p));
          item.sets = std::min(item.sets, a.sets);
        }
        item.adjusted = what + "; the rules say: " + a.reason;
      }
      if (e->load != Load::Bodyweight) item.weight_kg = snap(item.weight_kg, unit(*e) / 2);
      kept.push_back(item);
    }
    day.items = kept;
  }
  std::erase_if(proposed.days, [](const PlanDay& d) { return d.items.empty(); });
  proposed.source = c.adjusted ? "model+rules" : "model";
  c.plan = proposed;
  return c;
}

Plan rules_plan(const Profile& p, const std::vector<ExerciseHistory>& history, int fatigue, const std::optional<Plan>& current) {
  Plan plan;
  plan.source = "rules";
  plan.summary = "Linear progression from the log.";
  if (current) {
    plan.days = current->days;
  } else {
    bool barbell = p.equipment.contains("barbell");
    auto item = [&](const QString& id, int sets, int reps) {
      const ExerciseLoad* e = find_load(id);
      return PlanItem{id, sets, reps, e ? start_weight(*e, p) : 0, e && e->load == Load::Barbell ? 150 : 90, "starting weight", {}, {}};
    };
    QString lower = barbell ? "squats" : "lunges", hinge = barbell ? "deadlift" : "lunges";
    std::vector<PlanDay> all = {
        {"Day A", {item(lower, 3, 5), item("dumbbell_shoulder_press", 3, 8), item("dumbbell_rows", 3, 10), item("situps", 2, 15)}},
        {"Day B", {item(hinge, barbell ? 1 : 3, 5), item("pushups", 3, 10), item("lunges", 2, 10), item("bicep_curls", 2, 12)}},
        {"Day C", {item(lower, 3, 5), item("dumbbell_rows", 3, 10), item("lateral_shoulder_raises", 2, 15), item("tricep_extensions", 2, 12)}},
        {"Day D", {item("pushups", 3, 12), item("dumbbell_shoulder_press", 3, 10), item("jumping_jacks", 2, 30), item("situps", 2, 15)}},
    };
    for (int d = 0; d < std::clamp(p.days, 1, 4); ++d) plan.days.push_back(all[size_t(d)]);
  }
  for (auto& day : plan.days)
    for (auto& item : day.items) {
      const ExerciseHistory* h = history_of(history, item.exercise);
      ExerciseHistory empty{item.exercise, {}};
      Advice a = advise(h ? *h : empty, fatigue, item);
      item.sets = a.sets, item.reps = a.reps, item.weight_kg = a.weight_kg, item.why = a.reason;
      item.adjusted.clear();
    }
  return plan;
}

}  // namespace spotter
