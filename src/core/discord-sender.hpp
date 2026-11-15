#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>
#include <functional>
namespace quickscore {
QString discordChannelId(const QString &input);
QJsonObject discordMessage(const QString &scoreLine, const QString &nonce);
class DiscordSender final : public QObject {
public:
  using Completion = std::function<void(bool, QString)>;
  explicit DiscordSender(QObject *parent = nullptr, QUrl testEndpoint = {});
  ~DiscordSender() override;
  bool busy() const { return !reply_.isNull(); }
  void cancel();
  void send(QString token, QString channel, QString content, Completion completion);

private:
  QNetworkAccessManager manager_;
  QPointer<QNetworkReply> reply_;
  QUrl testEndpoint_;
  uint64_t serial_ = 0;
};
} // namespace quickscore
