#include <QJsonDocument>
#include <QTest>

#include "planner.hpp"

using namespace spotter;

namespace {
// One exercise's sessions: (weight, reps planned, reps done per set, rpe, faults).
ExerciseHistory history(const QString& id, std::vector<std::tuple<double, int, std::vector<int>, double, int>> sessions) {
  ExerciseHistory h{id, {}};
  int day = 1;
  for (auto& [w, planned, done, rpe, faults] : sessions) {
    ExerciseHistory::Session s{QDate(2026, 10, day++), 4, {}};
    int n = 1;
    for (int d : done) s.sets.push_back({id, n++, planned, d, w, rpe, n == 2 ? faults : 0, 3});
    h.sessions.push_back(s);
  }
  return h;
}
PlanItem item(const QString& id, int sets, int reps, double w) { return {id, sets, reps, w, 90, "", {}}; }
Plan plan_of(std::vector<PlanItem> items) {
  Plan p;
  p.days = {PlanDay{"Day A", items}};
  return p;
}
}  // namespace

class TestPlanner : public QObject {
  Q_OBJECT
 private slots:
  void rules() {
    auto clean = history("squats", {{60, 5, {5, 5, 5}, 7, 0}});
    auto a = advise(clean, 3, {});
    QCOMPARE(a.decision, Decision::Increase);
    QCOMPARE(a.weight_kg, 65.0);

    QCOMPARE(advise(history("squats", {{60, 5, {5, 5, 3}, 8, 0}}), 3, {}).decision, Decision::Hold);
    QCOMPARE(advise(history("squats", {{60, 5, {5, 5, 5}, 9.5, 0}}), 3, {}).decision, Decision::Hold);
    QCOMPARE(advise(history("squats", {{60, 5, {5, 5, 5}, 7, 6}}), 3, {}).decision, Decision::Hold);

    auto twice = advise(history("squats", {{60, 5, {5, 4, 3}, 8, 0}, {60, 5, {5, 5, 4}, 8, 0}}), 3, {});
    QCOMPARE(twice.decision, Decision::Deload);
    QCOMPARE(twice.weight_kg, 52.5);  // 54 snapped down to the plates

    auto tired = advise(clean, 8, {});
    QCOMPARE(tired.decision, Decision::Reduce);
    QCOMPARE(tired.sets, 2);
    QCOMPARE(tired.weight_kg, 60.0);
    QCOMPARE(advise(clean, 9, {}).weight_kg, 52.5);

    auto push = advise(history("pushups", {{0, 12, {12, 12, 12}, 7, 0}}), 3, {});
    QCOMPARE(push.reps, 13);
    QCOMPARE(push.weight_kg, 0.0);
  }

  void keeps_a_plan_within_bounds() {
    std::vector<ExerciseHistory> h = {history("squats", {{60, 5, {5, 5, 5}, 7, 0}})};
    auto c = validate(plan_of({item("squats", 3, 5, 65)}), {}, h, 3, std::nullopt);
    QCOMPARE(c.adjusted, 0);
    QCOMPARE(c.plan.source, QString("model"));
    QCOMPARE(c.plan.days[0].items[0].weight_kg, 65.0);
  }

  void replaces_what_breaks_the_rules() {
    std::vector<ExerciseHistory> h = {history("squats", {{60, 5, {5, 5, 3}, 8, 0}}),
                                      history("pushups", {{0, 12, {12, 12, 12}, 7, 0}})};
    Plan p = plan_of({item("squats", 3, 5, 70), item("pushups", 3, 12, 10), item("bench_press", 3, 5, 60)});
    auto c = validate(p, {}, h, 3, std::nullopt);
    QCOMPARE(c.items, 2);
    QCOMPARE(c.adjusted, 2);
    QCOMPARE(c.plan.source, QString("model+rules"));
    const auto& items = c.plan.days[0].items;
    QCOMPARE(int(items.size()), 2);  // the unknown exercise is gone
    QCOMPARE(items[0].weight_kg, 60.0);  // missed reps: hold, not +10 kg
    QVERIFY(items[0].adjusted.contains("heavier"));
    QCOMPARE(items[1].weight_kg, 0.0);
  }

  void fatigue_caps_sets() {
    std::vector<ExerciseHistory> h = {history("dumbbell_rows", {{20, 10, {10, 10, 10}, 7, 0}})};
    auto c = validate(plan_of({item("dumbbell_rows", 3, 10, 20)}), {}, h, 8, std::nullopt);
    QCOMPARE(c.adjusted, 1);
    QCOMPARE(c.plan.days[0].items[0].sets, 2);
  }

  void first_plan_ceiling() {
    Profile p;
    p.bodyweight_kg = 60;
    auto c = validate(plan_of({item("squats", 3, 5, 100), item("squats", 3, 5, 30)}), p, {}, 3, std::nullopt);
    QCOMPARE(c.adjusted, 1);
    QVERIFY(c.plan.days[0].items[0].weight_kg <= 60 * 1.2 * 0.6);
    QCOMPARE(c.plan.days[0].items[1].weight_kg, 30.0);
  }

  void rules_plan_and_json() {
    Profile p;
    p.days = 3;
    Plan plan = rules_plan(p, {}, 3, std::nullopt);
    QCOMPARE(int(plan.days.size()), 3);
    for (const auto& d : plan.days)
      for (const auto& i : d.items) QVERIFY(find_load(i.exercise));
    auto back = Plan::fromJson(QJsonDocument::fromJson(QJsonDocument(plan.toJson()).toJson()).object());
    QVERIFY(back);
    QCOMPARE(back->days[1].items[0].exercise, plan.days[1].items[0].exercise);
    QVERIFY(!Plan::fromJson(QJsonObject{{"summary", "x"}}));
  }

  void prompt_mentions_the_log() {
    std::vector<ExerciseHistory> h = {history("squats", {{60, 5, {5, 5, 3}, 8, 0}})};
    QString prompt = plan_prompt({}, h, 7, std::nullopt);
    QVERIFY(prompt.contains("done 5/5/3 (missed 2 repetitions)"));
    QVERIFY(prompt.contains("Fatigue reported today: 7/10"));
    QVERIFY(plan_schema()["properties"].toObject().contains("days"));
  }
};

QTEST_GUILESS_MAIN(TestPlanner)
#include "test_planner.moc"
