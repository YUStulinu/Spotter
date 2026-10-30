#pragma once
// The planner's language model: llama.cpp's server with Qwen2.5-7B-Instruct, on this machine. The
// answer is held to a JSON schema (the server turns it into a grammar), so it always parses.
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <memory>
#include <optional>

namespace spotter {

struct LlmReply {
  QString text;
  double ms = 0;
};

class Llm {
 public:
  explicit Llm(QString url = "http://127.0.0.1:8092") : url_(std::move(url)) {}
  bool alive(int timeout_ms = 1000) const;
  // Temperature 0. Throws std::runtime_error on failure.
  LlmReply chat(const QString& system, const QString& user, const QJsonObject& schema = {}, int max_tokens = 1500,
                int timeout_ms = 180000) const;

 private:
  QString url_;
};

// Starts third_party/llama/llama-server with the model unless a server already answers on the port.
class LlmServer {
 public:
  LlmServer(QString model, int port = 8092);
  ~LlmServer();
  bool start(int wait_s = 120);  // true once the server answers
  static QString default_model();

 private:
  QString model_;
  int port_;
  std::unique_ptr<QProcess> process_;
};

}  // namespace spotter
