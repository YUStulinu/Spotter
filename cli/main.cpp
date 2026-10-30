// spotter-cli: the pipeline without a screen, for evaluation and for scripts.
//   spotter-cli pose <video> <out.kp> [--cpu] [--frames N]    skeletons of a video, and the speed
//   spotter-cli signals <in.kp> <out.csv>                       every repetition signal, per frame
//   spotter-cli count <in.kp> <exercise> <from s> <to s>        repetitions and form faults in a segment
//   spotter-cli run <in.kp|video> [--exercise id] [--cpu]       the live coach: sets, repetitions, faults (JSON lines)
//   spotter-cli count-grid <in.kp> <sets.csv> <grid.csv>        repetitions under candidate counter settings
//   spotter-cli plan-eval [--out results/planner.json]          the planner on scenarios, model against rules
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/videoio.hpp>

#include "classifier.hpp"
#include "form.hpp"
#include "llm.hpp"
#include "planner.hpp"
#include "pose.hpp"
#include "reps.hpp"
#include "session.hpp"
#include "skeleton.hpp"

using namespace spotter;

namespace {

std::string root_path(const std::string& rel) { return QDir(SPOTTER_ROOT).filePath(QString::fromStdString(rel)).toStdString(); }

bool flag(std::vector<std::string>& args, const std::string& name) {
  auto it = std::find(args.begin(), args.end(), name);
  if (it == args.end()) return false;
  args.erase(it);
  return true;
}

std::string option(std::vector<std::string>& args, const std::string& name, const std::string& fallback) {
  auto it = std::find(args.begin(), args.end(), name);
  if (it == args.end() || it + 1 == args.end()) return fallback;
  std::string v = *(it + 1);
  args.erase(it, it + 2);
  return v;
}

void print(const QJsonObject& o) { std::cout << QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString() << std::endl; }

QJsonObject rep_json(const Rep& r) { return {{"start", r.start}, {"far", r.far}, {"end", r.end}, {"range", r.range}}; }

// Frames of a video through the pose model; the time is the frame's position in the video.
std::vector<Frame> video_frames(const std::string& path, PoseEstimator& pose, long max_frames, bool report) {
  cv::VideoCapture cap(path);
  if (!cap.isOpened()) throw std::runtime_error("cannot open " + path);
  double fps = cap.get(cv::CAP_PROP_FPS);
  if (fps <= 1) fps = 30;
  std::vector<Frame> out;
  std::vector<double> pose_ms, det_ms, total_ms;
  cv::Mat img;
  long n = 0;
  auto t0 = std::chrono::steady_clock::now();
  while ((max_frames <= 0 || n < max_frames) && cap.read(img)) {
    auto f0 = std::chrono::steady_clock::now();
    PoseTiming t;
    Skeleton s = pose.run(img, &t);
    total_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count());
    pose_ms.push_back(t.pose_ms);
    if (t.detected) det_ms.push_back(t.detect_ms);
    out.push_back({n / fps, s});
    ++n;
    if (report && n % 3000 == 0) std::cerr << n << " frames\n";
  }
  if (report && !total_ms.empty()) {
    auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int found = int(std::count_if(out.begin(), out.end(), [](const Frame& f) { return f.s.found(); }));
    print({{"frames", double(n)}, {"device", QString::fromStdString(pose.device())}, {"video_fps", fps},
           {"median_frame_ms", median(total_ms)}, {"median_pose_ms", median(pose_ms)}, {"median_detect_ms", median(det_ms)},
           {"detector_runs", double(det_ms.size())}, {"throughput_fps", n / wall}, {"person_found", double(found) / double(n)}});
  }
  return out;
}

bool is_video(const std::string& path) {
  std::string ext = path.substr(path.find_last_of('.') + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == "mp4" || ext == "avi" || ext == "mov" || ext == "mkv" || ext == "webm";
}

int cmd_pose(std::vector<std::string> args) {
  bool cpu = flag(args, "--cpu");
  long frames = std::stol(option(args, "--frames", "0"));
  if (args.size() != 2) throw std::runtime_error("usage: pose <video> <out.kp> [--cpu] [--frames N]");
  PoseEstimator pose(root_path("third_party/models/pose"), !cpu);
  auto out = video_frames(args[0], pose, frames, true);
  write_keypoints(args[1], out);
  return 0;
}

int cmd_signals(std::vector<std::string> args) {
  if (args.size() != 2) throw std::runtime_error("usage: signals <in.kp> <out.csv>");
  auto frames = read_keypoints(args[0]);
  std::ofstream out(args[1]);
  out << "t,torso";
  for (int s = 0; s < int(Signal::Count); ++s) out << ',' << signal_name(Signal(s));
  out << ",view\n";
  // The standing torso, as the counter estimates it: the running maximum over the last 10 s.
  std::deque<std::pair<double, float>> torsos;
  for (const auto& f : frames) {
    float torso = Body::torso(f.s);
    while (!torsos.empty() && torsos.front().first < f.t - 10) torsos.pop_front();
    if (torso > 0) torsos.push_back({f.t, torso});
    float ref = 0;
    for (auto& [t, v] : torsos) ref = std::max(ref, v);
    out << f.t << ',' << ref;
    for (int s = 0; s < int(Signal::Count); ++s) {
      auto v = measure(Signal(s), f.s, ref);
      out << ',';
      if (v) out << *v;
    }
    View v = view_of(f.s);
    out << ',' << (v == View::Front ? "front" : v == View::Side ? "side" : "") << '\n';
  }
  return 0;
}

int cmd_count(std::vector<std::string> args) {
  if (args.size() != 4) throw std::runtime_error("usage: count <in.kp> <exercise> <from s> <to s>");
  auto frames = read_keypoints(args[0]);
  const Exercise* e = find_exercise(args[1]);
  if (!e) throw std::runtime_error("unknown exercise " + args[1]);
  double from = std::stod(args[2]), to = std::stod(args[3]);
  Smoother smooth;
  Track track(1 << 20);
  for (const auto& f : frames)
    if (f.t >= from && f.t <= to) track.add(f.t, smooth(f.t, f.s));
  RepCounter counter(*e);
  std::vector<Frame> all(track.frames().begin(), track.frames().end());
  QJsonArray reps, faults;
  for (const auto& f : all)
    if (auto r = counter.add(f.t, f.s)) {
      reps.append(rep_json(*r));
      std::vector<Frame> span;
      for (const auto& g : all)
        if (g.t >= r->start - 0.1 && g.t <= r->end + 0.1) span.push_back(g);
      for (const auto& fault : check_form(*e, *r, span))
        faults.append(QJsonObject{{"id", QString::fromStdString(fault.id)}, {"t", fault.t}, {"value", fault.value},
                                  {"detail", QString::fromStdString(fault.detail)}});
    }
  print({{"exercise", QString::fromStdString(e->id)}, {"count", counter.count()}, {"reps", reps}, {"faults", faults}});
  return 0;
}

// Counting with candidate settings, for choosing them (train/spotter_train/tune_reps.py):
// sets.csv has "from,to" per line (seconds), grid.csv "signal,direction,amplitude,count_at_far,min_rep_s";
// prints "set,grid,count" for every pair.
int cmd_count_grid(std::vector<std::string> args) {
  if (args.size() != 3) throw std::runtime_error("usage: count-grid <in.kp> <sets.csv> <grid.csv>");
  auto frames = read_keypoints(args[0]);
  auto rows = [](const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<std::vector<std::string>> out;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      std::vector<std::string> cells;
      std::stringstream ss(line);
      std::string c;
      while (std::getline(ss, c, ',')) cells.push_back(c);
      out.push_back(cells);
    }
    return out;
  };
  auto sets = rows(args[1]);
  std::vector<Exercise> grid;
  for (const auto& r : rows(args[2])) {
    Exercise e{"grid", "grid", Signal::Count, std::stof(r[1]), std::stof(r[2]), r[3] == "1", std::stod(r[4])};
    for (int s = 0; s < int(Signal::Count); ++s)
      if (r[0] == signal_name(Signal(s))) e.signal = Signal(s);
    if (e.signal == Signal::Count) throw std::runtime_error("unknown signal " + r[0]);
    grid.push_back(e);
  }
  for (size_t si = 0; si < sets.size(); ++si) {
    double from = std::stod(sets[si][0]), to = std::stod(sets[si][1]);
    Smoother smooth;
    Track track(1 << 20);
    for (const auto& f : frames)
      if (f.t >= from && f.t <= to) track.add(f.t, smooth(f.t, f.s));
    for (size_t gi = 0; gi < grid.size(); ++gi) {
      RepCounter counter(grid[gi]);
      for (const auto& f : track.frames()) counter.add(f.t, f.s);
      std::cout << si << ',' << gi << ',' << counter.count() << '\n';
    }
  }
  return 0;
}

int cmd_run(std::vector<std::string> args) {
  std::string forced = option(args, "--exercise", "");
  bool cpu = flag(args, "--cpu");
  if (args.size() != 1) throw std::runtime_error("usage: run <in.kp|video> [--exercise id] [--cpu]");
  std::vector<Frame> frames;
  if (is_video(args[0])) {
    PoseEstimator pose(root_path("third_party/models/pose"), !cpu);
    frames = video_frames(args[0], pose, 0, false);
  } else {
    frames = read_keypoints(args[0]);
  }
  Classifier classifier(root_path("models/exercise.onnx"));
  Session session(&classifier);
  if (!forced.empty()) session.force(forced);
  auto report = [](const std::vector<Event>& events) {
    static const char* kinds[] = {"set_started", "rep", "fault", "set_ended"};
    for (const auto& e : events) {
      QJsonObject o{{"event", kinds[e.kind]}, {"t", e.t}, {"exercise", QString::fromStdString(e.exercise)}, {"reps", e.reps}};
      if (e.rep) o["rep"] = rep_json(*e.rep);
      if (e.fault) o["fault"] = QString::fromStdString(e.fault->id), o["cue"] = QString::fromStdString(e.fault->cue);
      print(o);
    }
  };
  for (const auto& f : frames) report(session.add(f.t, f.s));
  report(session.finish());
  return 0;
}

}  // namespace

int cmd_plan_eval(std::vector<std::string> args);  // plan_eval.cpp

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) {
    std::cerr << "usage: spotter-cli pose|signals|count|run|plan-eval ...\n";
    return 2;
  }
  std::string cmd = args[0];
  args.erase(args.begin());
  try {
    if (cmd == "pose") return cmd_pose(args);
    if (cmd == "signals") return cmd_signals(args);
    if (cmd == "count") return cmd_count(args);
    if (cmd == "run") return cmd_run(args);
    if (cmd == "count-grid") return cmd_count_grid(args);
    if (cmd == "plan-eval") return cmd_plan_eval(args);
    std::cerr << "unknown command " << cmd << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
