// Spotter: a gym coach that watches through the camera.
//   spotter [--video file [--from s] [--fast]] [--page train|plan|progress] [--screenshot file.png --after seconds]
// --video plays a recording instead of opening the camera (--fast: as fast as the pipeline goes);
// --log uses another workout log, --date logs a replay under that day; --screenshot saves the window after some seconds and quits (the README's pictures).
#include <QCommandLineParser>
#include <QDate>
#include <QDir>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "coach.hpp"
#include "history.hpp"

int main(int argc, char** argv) {
  // A dark Material look to go with the camera picture.
  qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
  qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#4cc38a");
  QGuiApplication app(argc, argv);
  QQuickStyle::setStyle("Material");
  app.setApplicationName("Spotter");
  app.setOrganizationName("Spotter");
  QCommandLineParser cli;
  cli.addHelpOption();
  QCommandLineOption video("video", "Play this video instead of the camera.", "file");
  QCommandLineOption fast("fast", "Play the video as fast as it can be processed.");
  QCommandLineOption from("from", "Start the video at this second.", "seconds", "0");
  QCommandLineOption page("page", "Open this page: train, plan or progress.", "page", "train");
  QCommandLineOption shot("screenshot", "Save the window to this PNG and quit.", "file");
  QCommandLineOption after("after", "Seconds before the screenshot.", "seconds", "5");
  QCommandLineOption plannedSet("plan-set", "Count this exercise (exercise:reps:kg).", "set");
  QCommandLineOption database("log", "The workout log (SQLite).", "file");
  QCommandLineOption date("date", "Log a replayed recording under this date (YYYY-MM-DD).", "date");
  QCommandLineOption quit("quit-at-end", "Quit when the video ends.");
  QCommandLineOption makePlan("make-plan", "Make a plan at start, with this fatigue (1-10).", "fatigue");
  cli.addOptions({video, fast, from, page, shot, after, plannedSet, database, date, quit, makePlan});
  cli.process(app);

  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  Coach coach(cli.isSet(database) ? cli.value(database) : QDir(dir).filePath("spotter.sqlite"));
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("startPage", cli.value(page));
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("Spotter", "Main");

  if (cli.isSet(makePlan))
    if (auto* h = engine.singletonInstance<History*>("Spotter", "History"))
      h->makePlan("strength", 3, "beginner", 75, true, cli.value(makePlan).toInt());
  if (cli.isSet(date)) coach.setDate(QDate::fromString(cli.value(date), Qt::ISODate));
  if (cli.isSet(quit))
    QObject::connect(&coach, &Coach::runningChanged, &app, [&coach] { if (!coach.running()) QTimer::singleShot(500, [] { QCoreApplication::quit(); }); });
  if (cli.isSet(plannedSet)) {
    auto parts = cli.value(plannedSet).split(':');
    coach.planSet(parts.value(0), parts.value(1, "10").toInt(), parts.value(2, "0").toDouble());
  }
  if (cli.isSet(video)) coach.openVideo(QUrl::fromLocalFile(cli.value(video)), !cli.isSet(fast), cli.value(from).toDouble());
  if (cli.isSet(shot)) {
    QString file = cli.value(shot);
    QTimer::singleShot(int(cli.value(after).toDouble() * 1000), &app, [&engine, file] {
      if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) w->grabWindow().save(file);
      QCoreApplication::quit();
    });
  }
  int code = app.exec();
  coach.stop();
  return code;
}
