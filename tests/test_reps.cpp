#include <QTest>

#include "synth.hpp"
#include "reps.hpp"

using namespace spotter;

namespace {
int count(const std::string& id, const std::vector<Frame>& frames) {
  RepCounter c(*find_exercise(id));
  for (const auto& f : frames) c.add(f.t, f.s);
  return c.count();
}
}  // namespace

class TestReps : public QObject {
  Q_OBJECT
 private slots:
  void every_exercise_is_known() {
    for (const char* id : {"squats", "lunges", "pushups", "situps", "jumping_jacks", "bicep_curls", "tricep_extensions",
                           "dumbbell_rows", "dumbbell_shoulder_press", "lateral_shoulder_raises", "deadlift"})
      QVERIFY2(find_exercise(id), id);
    QVERIFY(!find_exercise("bench_press"));
  }

  void counts_squats() {
    QCOMPARE(count("squats", synth::reps(synth::squat, 8, 2.0)), 8);
    QCOMPARE(count("squats", synth::reps(synth::squat, 3, 1.2)), 3);
    QCOMPARE(count("squats", synth::reps(synth::squat, 12, 3.0, 15)), 12);  // a slow camera
  }

  void counts_curls() { QCOMPARE(count("bicep_curls", synth::reps(synth::curl, 10, 1.5)), 10); }

  void ignores_small_movements() {
    // Shifting a little at the top is not a squat.
    auto frames = synth::reps([](float d) { return synth::squat(0.15f * d); }, 6, 2.0);
    QCOMPARE(count("squats", frames), 0);
  }

  void ignores_jitter_and_missing_frames() {
    auto frames = synth::reps(synth::squat, 6, 2.0);
    for (size_t i = 0; i < frames.size(); ++i) {
      for (auto& k : frames[i].s.k) k.y += float((i * 7919) % 9) - 4.f;
      if (i % 11 == 0) frames[i].s = Skeleton{};  // the pose model lost the person
    }
    QCOMPARE(count("squats", frames), 6);
  }

  void a_rep_has_times() {
    RepCounter c(*find_exercise("squats"));
    for (const auto& f : synth::reps(synth::squat, 1, 2.0)) c.add(f.t, f.s);
    QCOMPARE(c.count(), 1);
    const Rep& r = c.reps()[0];
    QVERIFY(r.start >= 1.4 && r.start < r.far && r.far < r.end && r.end < 3.6);
    QVERIFY(std::abs(r.far - 2.5) < 0.2);  // the bottom, halfway through the cycle
  }

  void deadlift_counts_at_lockout() {
    // From the floor (hips bent), up to standing, and down again.
    auto hinge = [](float d) {
      Skeleton s = synth::standing(true);
      float bend = 1 - d;  // starts bent
      for (int j : {Nose, LEye, REye, LEar, REar, LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist}) {
        s.k[size_t(j)].x += 200 * bend;
        s.k[size_t(j)].y += 150 * bend;
      }
      return s;
    };
    RepCounter c(*find_exercise("deadlift"));
    std::optional<Rep> first;
    double counted_at = 0;
    for (const auto& f : synth::reps(hinge, 3, 3.0))
      if (auto r = c.add(f.t, f.s); r && !first) first = r, counted_at = f.t;
    QCOMPARE(c.count(), 3);
    QVERIFY(counted_at < 1.5 + 2.0);  // before coming back down
  }
};

QTEST_GUILESS_MAIN(TestReps)
#include "test_reps.moc"
