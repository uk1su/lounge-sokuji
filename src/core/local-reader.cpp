#include "local-reader.hpp"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

namespace quickscore {
namespace {
Analysis failed(Analysis captured, const QString &error) {
  captured.error = error;
  captured.standings.clear();
  captured.evidence.clear();
  for (int rank = 1; rank <= 12; ++rank) {
    captured.standings.push_back({rank, {}, 0});
    captured.evidence.push_back({});
  }
  return captured;
}
QJsonObject readConfiguration(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
    return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
bool integer(const QJsonValue &value, int expected) {
  return value.isDouble() && value.toDouble() == expected;
}
} // namespace

Analysis parseLocalReply(const QJsonObject &reply, Analysis captured,
                         const std::vector<std::string> &tags) {
  const QString bad =
      QStringLiteral("ローカル判定の回答形式を検証できません。手動で確認してください。");
  if (!reply.value("error").toString().isEmpty())
    return failed(std::move(captured), QStringLiteral("ローカル判定に失敗しました: ") +
                                           reply.value("error").toString().left(500));
  if (!captured.complete() || !validateTeamTags(tags) || tags.size() < 2 || tags.size() > 6 ||
      !integer(reply.value("schema"), 1) ||
      reply.value("model").toString() != "visual-jev-4b-nf4" || !reply.value("rows").isArray() ||
      reply.value("rows").toArray().size() != 12)
    return failed(std::move(captured), bad);
  captured.error.clear();
  captured.standings.clear();
  captured.evidence.clear();
  const auto rows = reply.value("rows").toArray();
  for (int index = 0; index < 12; ++index) {
    const auto row = rows[index].toObject();
    const auto probabilities = row.value("probabilities").toArray();
    const auto selected = row.value("choice");
    if (!integer(row.value("rank"), index + 1) || !selected.isDouble() ||
        std::floor(selected.toDouble()) != selected.toDouble() || selected.toDouble() < -1 ||
        selected.toDouble() >= double(tags.size()) || !row.value("readable").isBool() ||
        !row.value("probabilities").isArray() || probabilities.size() != int(tags.size()) + 1)
      return failed(std::move(captured), bad);
    double sum = 0, best = -1, second = -1;
    int winner = -1;
    for (int candidate = 0; candidate < probabilities.size(); ++candidate) {
      const auto value = probabilities[candidate];
      const double p = value.toDouble(-1);
      if (!value.isDouble() || !std::isfinite(p) || p < 0 || p > 1)
        return failed(std::move(captured), bad);
      sum += p;
      if (p > best) {
        second = best;
        best = p;
        winner = candidate;
      } else if (p > second)
        second = p;
    }
    if (std::abs(sum - 1) > .001 || selected.toInt() != (winner == int(tags.size()) ? -1 : winner))
      return failed(std::move(captured), bad);
    RowEvidence evidence;
    evidence.method = QStringLiteral("Visual Jev / 要確認");
    evidence.similarity = best;
    std::string tag;
    if (winner < int(tags.size())) {
      evidence.closestTag = tags[size_t(winner)];
      evidence.closestScore = best;
      evidence.text =
          QStringLiteral("候補: ") + initialIdentifier(QString::fromStdString(evidence.closestTag));
      // These are conservative routing thresholds, not calibrated accuracy.
      if (row.value("readable").toBool() && best >= .90 && best - second >= .25) {
        tag = evidence.closestTag;
        evidence.method = QStringLiteral("Visual Jev / 候補一致");
      }
    }
    captured.standings.push_back({index + 1, tag, tag.empty() ? 0 : best});
    captured.evidence.push_back(std::move(evidence));
  }
  return captured;
}

LocalReader::LocalReader(QString configPath, QString workerPath, QObject *parent)
    : QObject(parent), configPath_(std::move(configPath)), workerPath_(std::move(workerPath)) {
  deadline_.setSingleShot(true);
  connect(&deadline_, &QTimer::timeout, this, [this] {
    fail(warming_ ? QStringLiteral("ローカルモデルの起動が180秒以内に完了しませんでした。")
                  : QStringLiteral("ローカル判定が30秒以内に完了しませんでした。"));
  });
  connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { consume(); });
  connect(&process_, &QProcess::readyReadStandardError, this, [this] {
    diagnostic_ += process_.readAllStandardError();
    diagnostic_ = diagnostic_.right(4096);
  });
  connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      fail(QStringLiteral("ローカルモデルを起動できません: ") + process_.errorString());
  });
  connect(
      &process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this](int, QProcess::ExitStatus) {
        if (ready_ || busy())
          fail(QStringLiteral("ローカルモデルが終了しました。集計を停止して再開してください。 ") +
               QString::fromUtf8(diagnostic_).right(600));
      });
#ifdef _WIN32
  process_.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
}
LocalReader::~LocalReader() { cancel(); }

QString LocalReader::configurationError() const {
  const auto config = readConfiguration(configPath_);
  if (!integer(config.value("schema"), 1) || !QFileInfo::exists(workerPath_) ||
      !QFileInfo::exists(config.value("pythonPath").toString()) ||
      !QFileInfo::exists(config.value("modelDirectory").toString() + "/config.json") ||
      !QFileInfo::exists(config.value("adapterDirectory").toString() + "/adapter_config.json"))
    return QStringLiteral("ローカルモデル未準備です。「ローカルモデルを準備」を押して、"
                          "完了後に集計を開始してください。");
  return {};
}
void LocalReader::prepare(std::function<void(QString)> completion) {
  if (ready_) {
    completion({});
    return;
  }
  if (busy()) {
    completion(QStringLiteral("ローカルモデルを準備中です。"));
    return;
  }
  const auto error = configurationError();
  if (!error.isEmpty()) {
    completion(error);
    return;
  }
  const auto config = readConfiguration(configPath_);
  prepared_ = std::move(completion);
  warming_ = true;
  output_.clear();
  diagnostic_.clear();
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("HF_HUB_OFFLINE", "1");
  environment.insert("TRANSFORMERS_OFFLINE", "1");
  environment.insert("PYTHONIOENCODING", "utf-8");
  environment.remove("GEMINI_API_KEY");
  environment.remove("GOOGLE_API_KEY");
  process_.setProcessEnvironment(environment);
  process_.start(config.value("pythonPath").toString(),
                 {"-u", workerPath_, "--config", configPath_});
  deadline_.start(180000);
}
void LocalReader::read(Analysis captured, std::vector<std::string> tags, Completion completion) {
  if (!ready_ || busy()) {
    completion(
        failed(std::move(captured), QStringLiteral("ローカルモデルの準備が完了していません。")));
    return;
  }
  captured_ = std::move(captured);
  tags_ = std::move(tags);
  completion_ = std::move(completion);
  QJsonArray initials;
  for (const auto &tag : tags_) {
    // Show the model the registered spelling, rather than changing M to m or
    // hiragana to katakana. Team identity still uses the shared normalizer.
    const auto text =
        QString::fromStdString(tag).trimmed().normalized(QString::NormalizationForm_KC);
    QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, text);
    initials.append(text.left(boundary.toNextBoundary()));
  }
  QByteArray png;
  QBuffer stream(&png);
  stream.open(QIODevice::WriteOnly);
  if (!captured_.image.save(&stream, "PNG")) {
    fail(QStringLiteral("ローカル判定用の画像を作成できません。"));
    return;
  }
  const auto bytes = QJsonDocument(QJsonObject{{"id", QString::number(++serial_)},
                                               {"initials", initials},
                                               {"image_png", QString::fromLatin1(png.toBase64())}})
                         .toJson(QJsonDocument::Compact) +
                     '\n';
  if (bytes.size() > 6 * 1024 * 1024 || process_.write(bytes) != bytes.size()) {
    fail(QStringLiteral("ローカルモデルへ画像を渡せません。"));
    return;
  }
  deadline_.start(30000);
}
void LocalReader::consume() {
  output_ += process_.readAllStandardOutput();
  if (output_.size() > 256 * 1024) {
    fail(QStringLiteral("ローカル回答が大きすぎます。"));
    return;
  }
  int newline;
  while ((newline = output_.indexOf('\n')) >= 0) {
    const auto line = output_.left(newline);
    output_.remove(0, newline + 1);
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
      fail(QStringLiteral("ローカルモデルの通信形式が不正です。"));
      return;
    }
    const auto object = doc.object();
    if (warming_) {
      if (object.value("event").toString() != "ready" || !integer(object.value("schema"), 1)) {
        fail(QStringLiteral("ローカルモデルの起動応答が不正です。"));
        return;
      }
      warming_ = false;
      ready_ = true;
      deadline_.stop();
      auto callback = std::move(prepared_);
      if (callback)
        callback({});
    } else if (completion_) {
      if (object.value("id").toString() != QString::number(serial_)) {
        fail(QStringLiteral("ローカル回答の識別子が一致しません。"));
        return;
      }
      finish(parseLocalReply(object, std::move(captured_), tags_));
    } else {
      fail(QStringLiteral("要求していないローカル回答を受信しました。"));
      return;
    }
  }
}
void LocalReader::finish(Analysis result) {
  deadline_.stop();
  auto callback = std::move(completion_);
  if (callback)
    callback(std::move(result));
}
void LocalReader::fail(const QString &error) {
  auto callback = std::move(completion_);
  auto prepared = std::move(prepared_);
  auto result = failed(std::move(captured_), error);
  cancel();
  if (prepared)
    prepared(error);
  if (callback)
    callback(std::move(result));
}
void LocalReader::cancel() {
  ++serial_;
  ready_ = warming_ = false;
  completion_ = {};
  prepared_ = {};
  deadline_.stop();
  if (process_.state() != QProcess::NotRunning) {
    process_.kill();
    process_.waitForFinished(300);
  }
  output_.clear();
}
} // namespace quickscore
