#include "speech.hpp"

#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#ifdef _WIN32
#include <sapi.h>
#include <windows.h>
#endif

namespace spotter {

QString spoken_number(int n) {
  static const char* words[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
                                "nineteen", "twenty"};
  if (n >= 0 && n <= 20) return words[n];
  return QString::number(n);
}

#ifdef _WIN32
struct Speech::Impl {
  ISpVoice* voice = nullptr;
  bool com = false;
  Impl() {
    com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&voice)))) voice = nullptr;
    if (voice) voice->SetRate(2);  // a coach talks briskly
  }
  ~Impl() {
    if (voice) voice->Release();
    if (com) CoUninitialize();
  }
  bool available() const { return voice != nullptr; }
  void say(const QString& text, bool interrupt) {
    if (!voice) return;
    DWORD flags = SPF_ASYNC | SPF_IS_NOT_XML | (interrupt ? SPF_PURGEBEFORESPEAK : 0);
    voice->Speak(reinterpret_cast<const wchar_t*>(text.utf16()), flags, nullptr);
  }
};
#else
struct Speech::Impl {
  QString program;
  QStringList args;
  QProcess* current = nullptr;
  Impl() {
    if (!(program = QStandardPaths::findExecutable("spd-say")).isEmpty()) args = {"-r", "30"};
    else if (!(program = QStandardPaths::findExecutable("espeak-ng")).isEmpty()) args = {"-s", "190"};
  }
  ~Impl() { delete current; }
  bool available() const { return !program.isEmpty(); }
  void say(const QString& text, bool interrupt) {
    if (program.isEmpty()) return;
    if (current && interrupt) current->kill(), current->waitForFinished(200);
    delete current;
    current = new QProcess;
    current->start(program, args + QStringList{text});
  }
};
#endif

Speech::Speech() : impl_(std::make_unique<Impl>()) {}
Speech::~Speech() = default;
bool Speech::available() const { return impl_->available(); }
void Speech::say(const QString& text, bool interrupt) {
  if (enabled_) impl_->say(text, interrupt);
}

}  // namespace spotter
