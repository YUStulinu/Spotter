#include <QTest>

#include "form.hpp"
#include "synth.hpp"

using namespace spotter;

namespace {
std::vector<std::string> faults(const std::string& id, const std::vector<Frame>& frames) {
  const Exercise& e = *find_exercise(id);
  RepCounter c(e);
  std::vector<std::string> out;
  for (const auto& f : frames)
    if (auto r = c.add(f.t, f.s)) {
      std::vector<Frame> span;
      for (const auto& g : frames)
        if (g.t >= r->start - 0.1 && g.t <= r->end + 0.1) span.push_back(g);
      for (const auto& fault : check_form(e, *r, span)) out.push_back(fault.id);
    }
  return out;
}

bool has(const std::vector<std::string>& v, const std::string& id) { return std::find(v.begin(), v.end(), id) != v.end(); }
}  // namespace

class TestForm : public QObject {
  Q_OBJECT
 private slots:
  void view() {
    QVERIFY(view_of(synth::standing()) == View::Front);
    QVERIFY(view_of(synth::standing(true)) == View::Side);
  }

  void clean_squats_pass() {
    auto f = faults("squats", synth::reps(synth::squat, 4, 2.0));
    QVERIFY2(f.empty(), f.empty() ? "" : f[0].c_str());
  }

  void knees_caving_in() {
    auto f = faults("squats", synth::reps([](float d) { return synth::squat_with(d, 1.0f); }, 4, 2.0));
    QCOMPARE(int(std::count(f.begin(), f.end(), "knees_in")), 4);
  }

  void shallow_squats() {
    auto f = faults("squats", synth::reps([](float d) { return synth::squat(0.6f * d); }, 3, 2.0));
    QVERIFY(has(f, "depth"));
  }

  void curl_swing() {
    auto swinging = [](float d) {
      Skeleton s = synth::curl(d);
      for (int j : {Nose, LEye, REye, LEar, REar, LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist}) s.k[size_t(j)].x -= 90 * d;
      return s;
    };
    QVERIFY(has(faults("bicep_curls", synth::reps(swinging, 3, 1.6)), "swing"));
    QVERIFY(faults("bicep_curls", synth::reps(synth::curl, 3, 1.6)).empty());
  }

  void knee_check_needs_a_front_view() {
    // The same caving knees, seen from the side: the check stays quiet rather than guess.
    auto side = [](float d) {
      Skeleton s = synth::squat_with(d, 1.0f);
      Skeleton f = synth::standing(true);
      for (int j : {LShoulder, RShoulder}) s.k[size_t(j)].x = f.k[size_t(j)].x;
      return s;
    };
    QVERIFY(!has(faults("squats", synth::reps(side, 3, 2.0)), "knees_in"));
  }
};

QTEST_GUILESS_MAIN(TestForm)
#include "test_form.moc"
