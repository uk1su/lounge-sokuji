#include "jev-api-reader.hpp"
#include "credential-store.hpp"
#include <QBuffer>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <memory>
namespace quickscore {
namespace {
QString first(const std::string &tag) {
  const auto s = QString::fromStdString(tag).trimmed().normalized(QString::NormalizationForm_KC);
  QTextBoundaryFinder b(QTextBoundaryFinder::Grapheme, s);
  return s.left(b.toNextBoundary());
}
Analysis failed(Analysis frame, const QString &error) {
  frame.standings.clear();
  frame.evidence.clear();
  for (int row = 0; row < 12; ++row) {
    frame.standings.push_back({row + 1, {}, 0});
    frame.evidence.push_back({});
  }
  frame.error = error;
  return frame;
}
bool probability(const QJsonValue &v) {
  return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= 0 && v.toDouble() <= 1;
}
} // namespace
QImage jevMontage(const QImage &frame) {
  if (frame.size() != QSize(1920, 1080))
    return {};
  QImage image(480, 448, QImage::Format_RGB32);
  image.fill(QColor(32, 32, 32));
  QPainter p(&image);
  p.setPen(Qt::white);
  auto font = p.font();
  font.setPixelSize(14);
  p.setFont(font);
  for (int row = 0; row < 12; ++row) {
    const int x = (row % 3) * 160, y = (row / 3) * 112;
    p.drawText(x + 6, y + 17, QString("Rank %1").arg(row + 1));
    p.drawImage(QRect(x + 6, y + 23, 138, 81), frame.copy(1013, 85 + 78 * row, 92, 54));
  }
  return image;
}
QJsonObject jevRequest(const std::vector<std::string> &tags, const QString &reference) {
  QJsonObject criteria, questions;
  for (size_t i = 0; i < tags.size(); ++i)
    criteria.insert(QString("team%1").arg(i), first(tags[i]));
  criteria.insert("unknown", "Unreadable or no match.");
  for (int row = 0; row < 12; ++row)
    questions.insert(QString("rank%1").arg(row + 1),
                     QJsonObject{{"type", "choice"}, {"criteria", criteria},
                                 {"instructions", QString("First name character at Rank %1, row %2, column %3?")
                                                      .arg(row + 1).arg(row / 3 + 1).arg(row % 3 + 1)}});
  return {{"model", "jev-1.14.0"},
          {"state", QJsonObject{{"montage", reference}, {"rules", "Read only the FIRST character of each player name. Ignore suffixes and team counts. Choose unknown if obscured or ambiguous."}}},
          {"assets", QJsonObject{{reference, QJsonObject{{"type", "image"}, {"part", "montage"}}}}},
          {"questions", questions}};
}
Analysis parseJevResponse(const QJsonObject &reply, Analysis frame,
                          const std::vector<std::string> &tags) {
  frame = failed(std::move(frame), {});
  if (reply["model"].toString() != "jev-1.14.0" || !reply["answers"].isObject() ||
      reply["answers"].toObject().size() != 12)
    return failed(std::move(frame),
                  QStringLiteral("Jev APIのモデル名または12順位の応答形式が不正です。"));
  const auto answers = reply["answers"].toObject();
  for (int row = 0; row < 12; ++row) {
    const auto a = answers[QString("rank%1").arg(row + 1)].toObject();
    const auto probs = a["probabilities"].toObject();
    const auto choice = a["choice"].toString();
    bool valid = a["type"] == "choice" && probability(a["confidence"]) &&
                 probs.size() == int(tags.size() + 1);
    double best = -1, second = -1, total = 0;
    QString winner;
    for (size_t i = 0; i <= tags.size(); ++i) {
      const auto name = i == tags.size() ? QString("unknown") : QString("team%1").arg(i);
      const auto v = probs[name];
      valid = valid && probability(v);
      const auto value = v.toDouble(-1);
      total += value;
      if (value > best) {
        second = best;
        best = value;
        winner = name;
      } else
        second = std::max(second, value);
    }
    valid = valid && std::abs(total - 1) < .05 && winner == choice;
    if (!valid) {
      frame.error =
          QStringLiteral("Jev APIの確率または順位の応答が不正です。手動確認してください。");
      continue;
    }
    auto &e = frame.evidence[size_t(row)];
    e.method = "Jev API / " + reply["model"].toString();
    e.similarity = best;
    e.text = QString("%1 / confidence %2 / margin %3")
                 .arg(choice)
                 .arg(a["confidence"].toDouble())
                 .arg(best - second);
    for (size_t i = 0; i < tags.size(); ++i)
      if (choice == QString("team%1").arg(i)) {
        e.closestTag = tags[i];
        e.closestScore = best;
        if (best >= .90 && best - second >= .25 && a["confidence"].toDouble() >= .90) {
          frame.standings[size_t(row)].tag = tags[i];
          frame.standings[size_t(row)].confidence = std::min(best, a["confidence"].toDouble());
        }
      }
  }
  return frame;
}
JevApiReader::JevApiReader(QString profile, QUrl endpoint)
    : profile_(std::move(profile)), endpoint_(std::move(endpoint)) {
  if (!endpoint_.isEmpty() && (endpoint_.scheme() != "http" || endpoint_.host() != "127.0.0.1"))
    endpoint_ = {};
}
JevApiReader::~JevApiReader() { cancel(); }
QString JevApiReader::key() const {
  auto k = loadStoredCredential(profile_, "JevApi");
  if (k.isEmpty())
    k = qEnvironmentVariable("TYPESAFE_API_KEY");
  return k.trimmed();
}
QString JevApiReader::configurationError() const {
  const auto k = key();
  return k.isEmpty() || k.contains('\n') || k.contains('\r')
             ? QStringLiteral("Jev APIキーを入力し「APIキーを保存」を押してください。")
             : QString{};
}
void JevApiReader::cancel() {
  ++serial_;
  if (reply_) {
    auto *r = reply_.data();
    reply_ = nullptr;
    r->disconnect(this);
    r->abort();
    r->deleteLater();
  }
}
void JevApiReader::read(Analysis frame, std::vector<std::string> tags, Completion done) {
  if (busy())
    return;
  lastUsage = {};
  lastModel.clear();
  const auto error = configurationError();
  const auto image = jevMontage(frame.image);
  QString tagError;
  if (!error.isEmpty() || image.isNull() || !validateTeamTags(tags, &tagError) || tags.size() < 2 ||
      tags.size() > 6) {
    done(failed(std::move(frame),
                error.isEmpty() ? QStringLiteral("画像またはタグの設定が無効です。") : error));
    return;
  }
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG", 85)) {
    done(failed(std::move(frame), QStringLiteral("送信用画像を作成できません。")));
    return;
  }
  auto *parts = new QHttpMultiPart(QHttpMultiPart::FormDataType);
  const auto ref = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QHttpPart json;
  json.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"request\"");
  json.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  json.setBody(QJsonDocument(jevRequest(tags, ref)).toJson(QJsonDocument::Compact));
  parts->append(json);
  QHttpPart photo;
  photo.setHeader(QNetworkRequest::ContentDispositionHeader,
                  "form-data; name=\"montage\"; filename=\"montage.png\"");
  photo.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
  photo.setBody(png);
  parts->append(photo);
  QNetworkRequest request(endpoint_.isEmpty() ? QUrl("https://api.typesafe.ai/v1/systemone")
                                              : endpoint_);
  request.setRawHeader("Authorization", "Bearer " + key().toUtf8());
  request.setRawHeader("TypeSafe-Danger-Zone-Images", "true");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(15000);
  auto *reply = manager_.post(request, parts);
  parts->setParent(reply);
  reply_ = reply;
  const auto serial = ++serial_;
  auto payload = std::make_shared<QByteArray>();
  constexpr qint64 limit = 1024 * 1024;
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
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, serial, payload, frame = std::move(frame), tags = std::move(tags),
           done = std::move(done)]() mutable {
            if (serial != serial_) {
              reply->deleteLater();
              return;
            }
            reply_ = nullptr;
            *payload += reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(*payload, &parse);
            if (status == 200 && reply->error() == QNetworkReply::NoError &&
                payload->size() <= limit && parse.error == QJsonParseError::NoError &&
                doc.isObject()) {
              lastUsage = doc.object()["usage"].toObject();
              lastModel = doc.object()["model"].toString();
              auto result = parseJevResponse(doc.object(), std::move(frame), tags);
              reply->deleteLater();
              done(std::move(result));
            } else {
              reply->deleteLater();
              done(failed(std::move(frame),
                          QStringLiteral(
                              "Jev APIに接続できませんでした (HTTP "
                              "%1)。キー・利用制限・接続を確認してください。自動再送はしません。")
                              .arg(status)));
            }
          });
}
} // namespace quickscore
