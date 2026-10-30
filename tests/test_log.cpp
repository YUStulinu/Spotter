#include <QTest>

#include "log.hpp"

using namespace spotter;

class TestLog : public QObject {
  Q_OBJECT
 private slots:
  void opens() {
    try { WorkoutLog log(":memory:"); } catch (const std::exception& e) { QFAIL(e.what()); }
  }

  void workouts_and_history() {
    WorkoutLog log(":memory:");
    qint64 a = log.startWorkout(QDate(2026, 10, 1), 4);
    log.addSet(a, {"squats", 1, 5, 5, 60, 7, 0, 3.1});
    log.addSet(a, {"squats", 2, 5, 4, 60, 9, 1, 3.4});
    log.addSet(a, {"pushups", 1, 12, 12, 0, 0, 0, 1.5});
    qint64 b = log.startWorkout(QDate(2026, 10, 4), 7);
    log.addSet(b, {"squats", 1, 5, 5, 62.5, 8, 0, 3.0});

    auto ws = log.workouts();
    QCOMPARE(int(ws.size()), 2);
    QCOMPARE(ws[0].date, QDate(2026, 10, 4));  // newest first
    QCOMPARE(int(ws[1].sets.size()), 3);

    auto h = log.history("squats");
    QCOMPARE(int(h.sessions.size()), 2);
    QCOMPARE(h.sessions[0].fatigue, 4);  // oldest first
    QCOMPARE(h.sessions[0].sets[1].reps_done, 4);
    QCOMPARE(h.sessions[1].sets[0].weight_kg, 62.5);
    QCOMPARE(log.exercises().first(), QString("squats"));
  }

  void history_is_limited_to_the_last_sessions() {
    WorkoutLog log(":memory:");
    for (int d = 1; d <= 12; ++d) log.addSet(log.startWorkout(QDate(2026, 9, d), 3), {"deadlift", 1, 5, 5, 80.0 + d, 0, 0, 0});
    auto h = log.history("deadlift", 4);
    QCOMPARE(int(h.sessions.size()), 4);
    QCOMPARE(h.sessions.back().sets[0].weight_kg, 92.0);
  }

  void estimated_max_and_plan() {
    WorkoutLog log(":memory:");
    log.addSet(log.startWorkout(QDate(2026, 10, 1), 3), {"squats", 1, 5, 5, 60, 0, 0, 0});
    auto m = log.estimatedMax("squats");
    QCOMPARE(int(m.size()), 1);
    QCOMPARE(m[0].second, 70.0);  // Epley: 60 * (1 + 5/30)
    QVERIFY(!log.plan());
    log.savePlan("{\"a\":1}");
    log.savePlan("{\"a\":2}");
    QCOMPARE(*log.plan(), QString("{\"a\":2}"));
  }

  void rejects_bad_values() {
    WorkoutLog log(":memory:");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, log.startWorkout(QDate(2026, 10, 1), 11));
  }
};

QTEST_GUILESS_MAIN(TestLog)
#include "test_log.moc"
