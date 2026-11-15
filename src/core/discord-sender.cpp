#include "discord-sender.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <memory>
namespace quickscore {
QString discordChannelId(const QString &input) {
  QString id = input.trimmed();
  const QUrl url(id);
  if (url.scheme() == "https" && url.host() == "discord.com") {
    const auto match = QRegularExpression("^/channels/[0-9]+/([0-9]+)$").match(url.path());
    if (match.hasMatch())
      id = match.captured(1);
  }
  return QRegularExpression("^[0-9]{15,22}$").match(id).hasMatch() ? id : QString{};
}
QJsonObject discordMessage(const QString &scoreLine, const QString &nonce) {
  return {{"content", scoreLine},
          {"allowed_mentions", QJsonObject{{"parse", QJsonArray{}}}},
          {"nonce", nonce},
          {"enforce_nonce", true}};
}
DiscordSender::DiscordSender(QObject *parent, QUrl endpoint)
    : QObject(parent), testEndpoint_(std::move(endpoint)) {
  if (!testEndpoint_.isEmpty() &&
      (testEndpoint_.scheme() != "http" || testEndpoint_.host() != "127.0.0.1"))
    testEndpoint_ = {};
}
DiscordSender::~DiscordSender() { cancel(); }
void DiscordSender::cancel() {
  ++serial_;
  if (reply_) {
    auto *reply = reply_.data();
    reply_ = nullptr;
    reply->abort();
    reply->deleteLater();
  }
}
void DiscordSender::send(QString token, QString channel, QString content, Completion completion) {
  // Repeated button clicks must not interrupt/resend a request in progress.
  if (busy())
    return;
  token = token.trimmed();
  if (token.startsWith("Bot ", Qt::CaseInsensitive))
    token = token.mid(4).trimmed();
  channel = discordChannelId(channel);
  if (channel.isEmpty() || token.isEmpty() || token.contains('\r') || token.contains('\n') ||
      content.isEmpty() || content.size() > 2000) {
    completion(
        false,
        QStringLiteral(
            "Botトークン・送信先チャンネルID・送信内容を確認してください（本文は2000文字まで）。"));
    return;
  }
  const auto serial = ++serial_;
  const auto url = testEndpoint_.isEmpty()
                       ? QUrl("https://discord.com/api/v10/channels/" + channel + "/messages")
                       : testEndpoint_;
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("Authorization", "Bot " + token.toUtf8());
  request.setRawHeader("User-Agent", "QuickScore/0.7.0 (OBS plugin)");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(15000);
  const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').left(25);
  auto *reply = manager_.post(
      request, QJsonDocument(discordMessage(content, nonce)).toJson(QJsonDocument::Compact));
  reply_ = reply;
  constexpr qint64 limit = 128 * 1024;
  auto payload = std::make_shared<QByteArray>();
  reply->setReadBufferSize(limit + 1);
  connect(reply, &QIODevice::readyRead, this, [reply, payload] {
    *payload += reply->readAll();
    if (payload->size() > limit)
      reply->abort();
  });
  QTimer::singleShot(15000, reply, [reply] {
    if (!reply->isFinished())
      reply->abort();
  });
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, serial, payload, completion = std::move(completion)] {
        if (serial != serial_) {
          reply->deleteLater();
          return;
        }
        reply_ = nullptr;
        *payload += reply->readAll();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(*payload, &parse);
        const bool ok = payload->size() <= limit && code == 200 &&
                        reply->error() == QNetworkReply::NoError &&
                        parse.error == QJsonParseError::NoError && document.isObject() &&
                        !discordChannelId(document.object()["id"].toString()).isEmpty();
        QString message;
        if (ok)
          message = QStringLiteral("Discordへ送信しました。");
        else if (code == 401)
          message = QStringLiteral("Botトークンが無効です。Discord設定を保存し直してください。");
        else if (code == 403)
          message = QStringLiteral("Botに送信権限がありません。対象チャンネルの閲覧・メッセージ送信"
                                   "権限を確認してください。");
        else if (code == 404)
          message = QStringLiteral(
              "送信先チャンネルが見つかりません。IDとBotの参加先を確認してください。");
        else if (code == 429)
          message = QStringLiteral(
              "Discordの送信制限に達しました。時間を置いて送信してください。自動再送はしません。");
        else
          message = QStringLiteral("送信結果を確認できませんでした (HTTP "
                                   "%1)。Discordに届いているか確認してから再送してください。")
                        .arg(code);
        reply->deleteLater();
        completion(ok, message);
      });
}
} // namespace quickscore
