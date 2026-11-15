#pragma once
#include "analysis.hpp"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <functional>

namespace quickscore {
Analysis parseLocalReply(const QJsonObject &reply, Analysis captured,
                         const std::vector<std::string> &tags);

class LocalReader final : public QObject {
public:
  using Completion = std::function<void(Analysis)>;
  LocalReader(QString configPath, QString workerPath, QObject *parent = nullptr);
  ~LocalReader() override;
  QString configurationError() const;
  bool ready() const { return ready_; }
  bool busy() const { return warming_ || bool(completion_); }
  void prepare(std::function<void(QString)> completion);
  void read(Analysis captured, std::vector<std::string> tags, Completion completion);
  void cancel();

private:
  void consume();
  void fail(const QString &error);
  void finish(Analysis result);
  QString configPath_, workerPath_;
  QProcess process_;
  QTimer deadline_;
  QByteArray output_, diagnostic_;
  bool ready_ = false, warming_ = false;
  uint64_t serial_ = 0;
  Completion completion_;
  std::function<void(QString)> prepared_;
  Analysis captured_;
  std::vector<std::string> tags_;
};
} // namespace quickscore
