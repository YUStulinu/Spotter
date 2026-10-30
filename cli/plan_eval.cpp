// The planner's evaluation: training logs built to cover each situation the progression rules
// distinguish (a clean session, a missed one, two missed in a row, a grinding one, one with form
// faults, high fatigue, a first plan), the model's plan for each, and how often the model's numbers
// go the way the rules go (up, same, down) and how often they break the rules' bounds before the
// check.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "llm.hpp"
#include "planner.hpp"

using namespace spotter;

namespace {

enum class Situation { Clean, MissedOnce, MissedTwice, Grinding, Faults };
const char* situation_name(Situation s) {
  static const char* n[] = {"clean", "missed_once", "missed_twice", "grinding", "form_faults"};
  return n[int(s)];
}

// Three sessions of an exercise ending in the situation.
ExerciseHistory make_history(const QString& id, Situation sit, double weight, int sets, int reps, std::mt19937& rng) {
  const ExerciseLoad* e = find_load(id);
  bool body = e->load == Load::Bodyweight;
  ExerciseHistory h{id, {}};
  QDate day(2026, 9, 1);
  for (int s = 0; s < 3; ++s) {
    bool last = s == 2, before = s == 1;
    double w = body ? 0 : weight - (2 - s) * (sit == Situation::Clean ? e->step_kg : 0);
    int r = body ? reps - (sit == Situation::Clean ? 2 - s : 0) : reps;
    ExerciseHistory::Session sess{day.addDays(s * 3), int(rng() % 4) + 3, {}};
    for (int k = 1; k <= sets; ++k) {
      SetRecord rec{id, k, r, r, w, 7.5, 0, 2.5};
      if (last && sit == Situation::MissedOnce && k == sets) rec.reps_done = r - 2;
      if ((last || before) && sit == Situation::MissedTwice && k >= sets - 1) rec.reps_done = r - 2;
      if (last && sit == Situation::Grinding) rec.rpe = 9.5;
      if (last && sit == Situation::Faults) rec.faults = r / 2;
      sess.sets.push_back(rec);
    }
    h.sessions.push_back(sess);
  }
  return h;
}

struct Scenario {
  QString name;
  Profile profile;
  int fatigue = 4;
  std::vector<ExerciseHistory> history;
  std::optional<Plan> current;
};

std::vector<Scenario> scenarios() {
  std::mt19937 rng(7);
  std::vector<Scenario> out;
  const std::vector<std::tuple<QString, double, int, int>> pool = {
      {"squats", 60, 3, 5}, {"deadlift", 80, 1, 5}, {"dumbbell_shoulder_press", 14, 3, 8}, {"dumbbell_rows", 20, 3, 10},
      {"bicep_curls", 10, 2, 12}, {"lunges", 12, 2, 10}, {"pushups", 0, 3, 12}, {"situps", 0, 2, 15}};
  const Situation sits[] = {Situation::Clean, Situation::MissedOnce, Situation::MissedTwice, Situation::Grinding, Situation::Faults};
  // Logged lifters: every exercise gets a situation, rotated so that each meets each; three fatigue levels.
  for (int round = 0; round < 5; ++round)
    for (int fatigue : {3, 8, 9}) {
      if (fatigue != 3 && round % 2 == 1) continue;  // fewer tired scenarios: they all look alike
      Scenario sc;
      sc.name = QString("log-r%1-f%2").arg(round).arg(fatigue);
      sc.fatigue = fatigue;
      sc.profile.days = 2;
      Plan current;
      current.source = "rules";
      PlanDay a{"Day A", {}}, b{"Day B", {}};
      for (size_t i = 0; i < pool.size(); ++i) {
        auto [id, w, sets, reps] = pool[i];
        Situation sit = sits[(i + size_t(round)) % 5];
        sc.history.push_back(make_history(id, sit, w, sets, reps, rng));
        sc.name += QString(" %1:%2").arg(id, situation_name(sit));
        PlanItem item{id, sets, reps, w, 90, "", {}};
        (i % 2 ? b : a).items.push_back(item);
      }
      current.days = {a, b};
      sc.current = current;
      out.push_back(sc);
    }
  // First plans: no log, different lifters.
  struct P { QString goal, exp; double bw; int days; QStringList eq; int fatigue; };
  for (const P& p : {P{"strength", "beginner", 60, 3, {"barbell", "dumbbells"}, 3}, P{"muscle", "intermediate", 85, 4, {"barbell", "dumbbells"}, 4},
                     P{"general", "beginner", 70, 2, {"dumbbells"}, 5}, P{"strength", "intermediate", 95, 3, {"barbell", "dumbbells"}, 8},
                     P{"general", "beginner", 55, 3, {"dumbbells"}, 2}}) {
    Scenario sc;
    sc.name = QString("first-%1-%2-%3kg").arg(p.goal, p.exp).arg(p.bw);
    sc.profile.goal = p.goal, sc.profile.experience = p.exp, sc.profile.bodyweight_kg = p.bw, sc.profile.days = p.days, sc.profile.equipment = p.eq;
    sc.fatigue = p.fatigue;
    out.push_back(sc);
  }
  return out;
}

const ExerciseHistory* find_h(const std::vector<ExerciseHistory>& hs, const QString& id) {
  for (const auto& h : hs)
    if (h.exercise == id) return &h;
  return nullptr;
}

}  // namespace

int cmd_plan_eval(std::vector<std::string> args) {
  QString out_path = QDir(SPOTTER_ROOT).filePath("results/planner.json");
  for (size_t i = 0; i + 1 < args.size(); ++i)
    if (args[i] == "--out") out_path = QString::fromStdString(args[i + 1]);
  LlmServer server(LlmServer::default_model());
  if (!server.start()) throw std::runtime_error("could not start the language model (scripts/fetch-deps.sh --llm)");
  Llm llm;
  QJsonArray rows;
  std::map<std::string, std::map<std::string, int>> agree;  // decision -> {agree, total}
  int items = 0, violations = 0, plans = 0, parsed = 0, items_first = 0, violations_first = 0, wrong_days = 0;
  std::vector<double> ms;
  for (const auto& sc : scenarios()) {
    QString prompt = plan_prompt(sc.profile, sc.history, sc.fatigue, sc.current);
    ++plans;
    LlmReply reply;
    try {
      reply = llm.chat(kPlanSystem, prompt, plan_schema(), 3000, 600000);
    } catch (const std::exception& e) {
      rows.append(QJsonObject{{"scenario", sc.name}, {"error", e.what()}});
      std::cerr << sc.name.left(40).toStdString() << ": " << e.what() << '\n';
      continue;
    }
    ms.push_back(reply.ms);
    QString error;
    auto plan = Plan::fromJson(QJsonDocument::fromJson(reply.text.toUtf8()).object(), &error);
    QJsonObject row{{"scenario", sc.name}, {"fatigue", sc.fatigue}, {"ms", reply.ms}};
    if (!plan) {
      row["error"] = error;
      rows.append(row);
      continue;
    }
    ++parsed;
    if (int(plan->days.size()) != sc.profile.days) ++wrong_days;
    Check check = validate(*plan, sc.profile, sc.history, sc.fatigue, sc.current);
    QJsonArray judged;
    for (const auto& day : plan->days)
      for (const auto& item : day.items) {
        const ExerciseLoad* e = find_load(item.exercise);
        if (!e) continue;
        const ExerciseHistory* h = find_h(sc.history, item.exercise);
        ExerciseHistory empty{item.exercise, {}};
        Advice a = advise(h ? *h : empty, sc.fatigue, item);
        // Would the check change this item?
        Plan one;
        one.days = {PlanDay{"day", {item}}};
        bool broke = validate(one, sc.profile, sc.history, sc.fatigue, sc.current).adjusted > 0;
        if (!h) {
          ++items_first;
          violations_first += broke;
          judged.append(QJsonObject{{"exercise", item.exercise}, {"weight", item.weight_kg}, {"sets", item.sets}, {"reps", item.reps}, {"broke_rules", broke}});
          continue;
        }
        ++items;
        violations += broke;
        PlanItem rule_item{item.exercise, a.sets, a.reps, a.weight_kg, 90, "", {}};
        int model_dir = direction(item, *h), rule_dir = direction(rule_item, *h);
        // Fewer sets at the same weight is "down" too.
        auto& slot = agree[decision_name(a.decision)];
        slot["total"]++;
        slot["agree"] += model_dir == rule_dir;
        slot["said"] += item.decision == decision_name(a.decision);
        judged.append(QJsonObject{{"exercise", item.exercise}, {"rule", decision_name(a.decision)}, {"model_direction", model_dir},
                                  {"rule_direction", rule_dir}, {"model_decision", item.decision}, {"weight", item.weight_kg}, {"rule_weight", a.weight_kg},
                                  {"sets", item.sets}, {"rule_sets", a.sets}, {"reps", item.reps}, {"rule_reps", a.reps},
                                  {"broke_rules", broke}, {"why", item.why}});
      }
    row["items"] = judged;
    row["problems"] = QJsonArray::fromStringList(check.problems);
    rows.append(row);
    std::cerr << sc.name.left(40).toStdString() << ": " << check.adjusted << " of " << check.items << " adjusted\n";
  }
  std::sort(ms.begin(), ms.end());
  QJsonObject by;
  int ag = 0, tot = 0, said = 0;
  for (auto& [d, m] : agree) {
    by[QString::fromStdString(d)] = QJsonObject{{"agree", m["agree"]}, {"said_the_same_decision", m["said"]}, {"total", m["total"]}};
    ag += m["agree"], tot += m["total"], said += m["said"];
  }
  QJsonObject summary{{"plans", plans}, {"parsed", parsed}, {"wrong_day_count", wrong_days},
                      {"logged_items", items}, {"direction_agreement", tot ? double(ag) / tot : 0},
                      {"decision_agreement", tot ? double(said) / tot : 0}, {"by_decision", by},
                      {"broke_rules", items ? double(violations) / items : 0},
                      {"first_plan_items", items_first}, {"first_plan_broke_rules", items_first ? double(violations_first) / items_first : 0},
                      {"median_ms", ms.empty() ? 0 : ms[ms.size() / 2]}};
  std::cout << QJsonDocument(summary).toJson().toStdString();
  QDir().mkpath(QFileInfo(out_path).path());
  QFile f(out_path);
  if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(QJsonObject{{"summary", summary}, {"scenarios", rows}}).toJson());
  return 0;
}
