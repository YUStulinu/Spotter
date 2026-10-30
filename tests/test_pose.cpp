#include <QDir>
#include <QTest>

#include <opencv2/imgcodecs.hpp>

#include "pose.hpp"
#include "skeleton.hpp"

using namespace spotter;

// tests/data/squat.jpg: a frame of MM-Fit's workout w20 at 640x360 (CC BY 4.0, see tests/data/README.md),
// with the dataset's own OpenPose keypoints for it, scaled to the same size.
static const float kOpenPose[17][2] = {{337, 91}, {343, 89}, {334, 88}, {356, 96}, {321, 148}, {363, 125}, {318, 128},
                                       {364, 166}, {307, 162}, {335, 191}, {311, 191}, {355, 210}, {324, 210},
                                       {357, 272}, {320, 271}, {364, 332}, {319, 325}};

class TestPose : public QObject {
  Q_OBJECT
 private slots:
  void finds_the_lifter() {
    cv::Mat img = cv::imread(QDir(SPOTTER_ROOT).filePath("tests/data/squat.jpg").toStdString());
    QVERIFY(!img.empty());
    PoseEstimator pose(QDir(SPOTTER_ROOT).filePath("third_party/models/pose").toStdString(), false);
    Skeleton s = pose.run(img);
    QVERIFY(s.found());
    QVERIFY(s.score > 0.5f);
    float torso = Body::torso(s);
    QVERIFY(torso > 60 && torso < 110);
    // Shoulders to ankles agree with OpenPose's within a quarter of a torso. (Ears and eyes differ by
    // convention between the two models, and the wrists are hidden in front of the body here.)
    for (int j : {LShoulder, RShoulder, LElbow, RElbow, LHip, RHip, LKnee, RKnee, LAnkle, RAnkle}) {
      float d = dist(pt(s.k[j]), {kOpenPose[j][0], kOpenPose[j][1]});
      QVERIFY2(d < 0.25f * torso, qPrintable(QString("joint %1 is %2 px off").arg(j).arg(d)));
    }
  }

  void tracks_without_detecting_every_frame() {
    cv::Mat img = cv::imread(QDir(SPOTTER_ROOT).filePath("tests/data/squat.jpg").toStdString());
    PoseEstimator pose(QDir(SPOTTER_ROOT).filePath("third_party/models/pose").toStdString(), false);
    int detections = 0;
    for (int i = 0; i < 10; ++i) {
      PoseTiming t;
      QVERIFY(pose.run(img, &t).found());
      detections += t.detected;
    }
    QCOMPARE(detections, 1);
  }

  void nobody_in_an_empty_frame() {
    cv::Mat empty(360, 640, CV_8UC3, cv::Scalar(200, 200, 200));
    PoseEstimator pose(QDir(SPOTTER_ROOT).filePath("third_party/models/pose").toStdString(), false);
    QVERIFY(!pose.run(empty).found());
  }
};

QTEST_GUILESS_MAIN(TestPose)
#include "test_pose.moc"
