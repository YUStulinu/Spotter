#pragma once
// The coach's voice during a set: short phrases ("seven", "knees out") spoken without waiting.
// Windows: the system's speech synthesiser (SAPI); elsewhere spd-say or espeak-ng when installed.
// A new phrase cuts the one still being spoken: the count matters more than finishing a cue.
#include <QString>
#include <memory>

namespace spotter {

class Speech {
 public:
  Speech();
  ~Speech();
  Speech(const Speech&) = delete;
  Speech& operator=(const Speech&) = delete;

  bool available() const;
  void say(const QString& text, bool interrupt = true);
  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool enabled_ = true;
};

// "7" -> "seven": counts are clearer spoken as words.
QString spoken_number(int n);

}  // namespace spotter
