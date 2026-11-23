#pragma once
#include "analysis.hpp"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <functional>
namespace quickscore {
QImage jevMontage(const QImage &frame);
QImage jevSingleCell(const QImage &frame, int row);
QJsonObject jevRequest(const std::vector<std::string> &tags, const QString &reference);
QJsonObject jevSingleRequest(const std::vector<std::string> &tags, int row,
                             const QString &reference);
Analysis parseJevResponse(const QJsonObject &reply, Analysis captured,
                          const std::vector<std::string> &tags);
class JevApiReader final : public QObject {
public:
  using Completion = std::function<void(Analysis)>;
  explicit JevApiReader(QString profile, QUrl testEndpoint = {});
  ~JevApiReader() override;
  QString configurationError() const;
  bool ready() const { return configurationError().isEmpty(); }
  bool busy() const { return !reply_.isNull(); }
  void prepare(std::function<void(QString)> done) { done(configurationError()); }
  void read(Analysis frame, std::vector<std::string> tags, Completion done);
  void cancel();
  QJsonObject lastUsage;
  QString lastModel;

private:
  void retryUnresolvedRows(Analysis frame, std::vector<std::string> tags, std::vector<int> rows,
                           size_t index, uint64_t serial, Completion done);
  QString key() const;
  QString profile_;
  QUrl endpoint_;
  QNetworkAccessManager manager_;
  QPointer<QNetworkReply> reply_;
  uint64_t serial_{};
};
} // namespace quickscore
