#include "llm.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThread>
#include <QTimer>
#include <stdexcept>

namespace spotter {
namespace {

QByteArray request(const QString& url, const QByteArray& body, int timeout_ms, int* status) {
  QNetworkAccessManager net;
  QNetworkRequest req{QUrl(url)};
  QNetworkReply* reply;
  if (body.isEmpty()) {
    reply = net.get(req);
  } else {
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    reply = net.post(req, body);
  }
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(timeout_ms);
  loop.exec();
  if (!reply->isFinished()) {
    reply->abort();
    reply->deleteLater();
    *status = -1;
    return {};
  }
  *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QByteArray data = reply->readAll();
  reply->deleteLater();
  return data;
}

}  // namespace

bool Llm::alive(int timeout_ms) const {
  int status = 0;
  request(url_ + "/health", {}, timeout_ms, &status);
  return status == 200;
}

LlmReply Llm::chat(const QString& system, const QString& user, const QJsonObject& schema, int max_tokens, int timeout_ms) const {
  QJsonObject body{{"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}},
                                           QJsonObject{{"role", "user"}, {"content", user}}}},
                   {"temperature", 0},
                   {"max_tokens", max_tokens},
                   {"cache_prompt", true}};
  if (!schema.isEmpty()) body["response_format"] = QJsonObject{{"type", "json_object"}, {"schema", schema}};
  QElapsedTimer timer;
  timer.start();
  int status = 0;
  QByteArray data = request(url_ + "/v1/chat/completions", QJsonDocument(body).toJson(QJsonDocument::Compact), timeout_ms, &status);
  if (status != 200) throw std::runtime_error(status < 0 ? "the language model did not answer in time" : "language model error " + std::to_string(status) + ": " + data.left(300).toStdString());
  auto reply = QJsonDocument::fromJson(data).object();
  QString text = reply["choices"].toArray().at(0).toObject()["message"].toObject()["content"].toString();
  return {text, double(timer.elapsed())};
}

LlmServer::LlmServer(QString model, int port) : model_(std::move(model)), port_(port) {}

LlmServer::~LlmServer() {
  if (process_) {
    process_->kill();
    process_->waitForFinished(3000);
  }
}

QString LlmServer::default_model() {
  QByteArray env = qgetenv("SPOTTER_LLM_MODEL");
  if (!env.isEmpty()) return QString::fromLocal8Bit(env);
  return QDir(SPOTTER_ROOT).filePath("third_party/models/Qwen2.5-7B-Instruct-Q4_K_M.gguf");
}

bool LlmServer::start(int wait_s) {
  Llm llm(QString("http://127.0.0.1:%1").arg(port_));
  if (llm.alive()) return true;
  QString exe = QDir(SPOTTER_ROOT).filePath("third_party/llama/llama-server");
#ifdef _WIN32
  exe += ".exe";
#endif
  if (!QFileInfo::exists(exe) || !QFileInfo::exists(model_)) return false;
  process_ = std::make_unique<QProcess>();
  process_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
  process_->start(exe, {"-m", model_, "--port", QString::number(port_), "-c", "8192", "-ngl", "99", "--parallel", "1", "--log-disable"});
  if (!process_->waitForStarted(10000)) return false;
  for (int i = 0; i < wait_s * 2; ++i) {
    if (llm.alive(500)) return true;
    if (process_->state() == QProcess::NotRunning) return false;
    QThread::msleep(500);
    QCoreApplication::processEvents();
  }
  return false;
}

}  // namespace spotter
