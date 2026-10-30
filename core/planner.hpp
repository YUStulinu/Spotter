#pragma once
// The training plan. A local language model writes the program and adjusts it after every workout
// from the log and the fatigue the lifter reports; a rule set checks its every number.
//
// The rules are a standard linear progression: add weight (or a repetition, for body-weight
// exercises) when every set was completed without grinding and with clean form; hold when a set was
// missed, felt very hard or the form slipped; take 10% off after two missed sessions in a row; and
// cut a set (and with very high fatigue 10% of the weight) when the lifter reports being worn out.
// The model's plan is kept where it stays within those bounds; where it does not, the rule's number
// replaces it and the plan says so.
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

#include "log.hpp"

namespace spotter {

enum class Load { Barbell, Dumbbell, Bodyweight };

struct ExerciseLoad {
  QString id;
  Load load;
  double step_kg;  // the usual increase; 0 for body weight (add a repetition instead)
  double max_bodyweight_ratio;  // a sane ceiling for a first plan, times body weight
};
const std::vector<ExerciseLoad>& exercise_loads();
const ExerciseLoad* find_load(const QString& id);

struct Profile {
  QString goal = "strength";   // strength, muscle, general
  int days = 3;                // per week
  QString experience = "beginner";  // beginner, intermediate
  double bodyweight_kg = 75;
  QStringList equipment = {"barbell", "dumbbells"};
  QString notes;
};

struct PlanItem {
  QString exercise;
  int sets = 3;
  int reps = 8;
  double weight_kg = 0;
  int rest_s = 90;
  QString why;          // the model's reason, or the rule's
  QString adjusted;     // non-empty when the rules changed the model's numbers: what and why
  QString decision;     // the model's: start, increase, hold, deload, reduce
};

struct PlanDay {
  QString name;
  std::vector<PlanItem> items;
};

struct Plan {
  QString summary;
  std::vector<PlanDay> days;
  QString source;  // "model", "model+rules" or "rules"
  QJsonObject toJson() const;
  static std::optional<Plan> fromJson(const QJsonObject& o, QString* error = nullptr);
};

enum class Decision { Start, Increase, Hold, Deload, Reduce };
const char* decision_name(Decision d);

// What the rules say for one exercise's next session.
struct Advice {
  Decision decision = Decision::Start;
  int sets = 3, reps = 8;
  double weight_kg = 0;
  QString reason;
};
Advice advise(const ExerciseHistory& h, int fatigue, const PlanItem& planned);

// The JSON schema the model must follow (llama.cpp turns it into a grammar).
QJsonObject plan_schema();

// The model's prompt: the profile, the exercises Spotter can see, the last sessions of each and
// today's fatigue.
QString plan_prompt(const Profile& p, const std::vector<ExerciseHistory>& history, int fatigue, const std::optional<Plan>& current);
extern const char* kPlanSystem;

// Keeps the model's numbers where they are within the rules' bounds, replaces them otherwise.
struct Check {
  Plan plan;
  int items = 0, adjusted = 0;
  QStringList problems;  // what was wrong with the model's plan
};
Check validate(Plan proposed, const Profile& p, const std::vector<ExerciseHistory>& history, int fatigue,
               const std::optional<Plan>& current);

// The plan from the rules alone: the fallback without a model, and the baseline in the evaluation.
Plan rules_plan(const Profile& p, const std::vector<ExerciseHistory>& history, int fatigue, const std::optional<Plan>& current);

// The direction a plan item takes compared with the last session: up, same, down.
int direction(const PlanItem& item, const ExerciseHistory& h);

}  // namespace spotter
