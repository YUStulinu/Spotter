#include <QTemporaryDir>
#include <QTest>

#include "synth.hpp"

using namespace spotter;

class TestSkeleton : public QObject {
  Q_OBJECT
 private slots:
  void angles() {
    QCOMPARE(qRound(angle({0, 0}, {0, 1}, {1, 1})), 90);
    QCOMPARE(qRound(angle({0, 0}, {0, 1}, {0, 2})), 180);
    Skeleton s = synth::standing();
    QVERIFY(*Body::knee_angle(s) > 170);
    QCOMPARE(qRound(Body::torso(s)), 200);
    QVERIFY(std::abs(*Body::hip_height(s, 200) - 1.8f) < 0.01f);
  }

  void hidden_joints_give_nothing() {
    Skeleton s = synth::standing();
    s.k[LAnkle].score = s.k[RAnkle].score = 0.1f;
    QVERIFY(!Body::hip_height(s, 200));
    QVERIFY(!Body::stance(s, 200));
    QVERIFY(Body::elbow_angle(s));
  }

  void track_resamples_to_30_hz() {
    Track track;
    for (int i = 0; i <= 20; ++i) track.add(i * 0.05, synth::squat(i / 20.f));  // 20 Hz for one second
    const auto& f = track.frames();
    QCOMPARE(int(f.size()), 31);
    QVERIFY(std::abs(f.back().t - 1.0) < 1e-6);
    // Halfway between two inputs, the joints are halfway too.
    QVERIFY(std::abs(f[15].s.k[LHip].y - synth::squat(0.5f).k[LHip].y) < 1.0f);
  }

  void track_restarts_after_a_gap() {
    Track track;
    track.add(0, synth::standing());
    track.add(0.1, synth::standing());
    track.add(5.0, synth::standing());
    QCOMPARE(int(track.frames().size()), 1);
  }

  void smoother_follows_and_steadies() {
    Smoother sm;
    Skeleton a = synth::standing();
    double jitter = 0;
    Skeleton prev;
    for (int i = 0; i < 60; ++i) {
      Skeleton noisy = a;
      noisy.k[Nose].x += (i % 2 ? 4.f : -4.f);
      Skeleton out = sm(i / 30.0, noisy);
      if (i > 30) jitter += std::abs(out.k[Nose].x - prev.k[Nose].x);
      prev = out;
    }
    QVERIFY(jitter / 29 < 4.0);  // the raw input jumps 8 px every frame
  }

  void keypoint_files_round_trip() {
    QTemporaryDir dir;
    std::vector<Frame> frames = {{0, synth::standing()}, {1.0 / 30, synth::squat(0.3f)}};
    std::string path = dir.filePath("a.kp").toStdString();
    write_keypoints(path, frames);
    auto back = read_keypoints(path);
    QCOMPARE(int(back.size()), 2);
    QVERIFY(std::abs(back[1].s.k[LHip].y - frames[1].s.k[LHip].y) < 0.01f);
    QVERIFY(std::abs(back[1].t - 1.0 / 30) < 1e-4);
  }
};

QTEST_GUILESS_MAIN(TestSkeleton)
#include "test_skeleton.moc"
