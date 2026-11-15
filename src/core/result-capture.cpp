#include "result-capture.hpp"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace quickscore {
namespace {
bool savePng(const QString &path, const QImage &image, QString &error) {
  QSaveFile file(path);
  // PNG remains lossless. Quality 85 selects zlib level 1 instead of spending
  // over a second compressing two result images at the default level.
  if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG", 85) || !file.commit()) {
    error = QStringLiteral("画像を保存できません: %1 (%2)").arg(path, file.errorString());
    return false;
  }
  return true;
}
} // namespace

QRect resultTableRect() { return QRect(820, 70, 1040, 945); }

ResultCapture saveResultCapture(const QString &directory, const QImage &frame,
                                QJsonObject context) {
  ResultCapture result;
  QElapsedTimer elapsed;
  elapsed.start();
  const auto save = [&] {
    if (directory.isEmpty() || frame.size() != QSize(1920, 1080)) {
      result.error = QStringLiteral("リザルト画像の保存先または画像サイズが無効です。");
      return;
    }
    if (!QDir().mkpath(directory)) {
      result.error = QStringLiteral("リザルト画像の保存フォルダーを作成できません: %1")
                         .arg(QDir::toNativeSeparators(directory));
      return;
    }
    const auto savedAt = QDateTime::currentDateTimeUtc();
    const auto stem = savedAt.toLocalTime().toString("yyyyMMdd-HHmmss-zzz") + "-" +
                      QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto base = QDir(directory).absoluteFilePath(stem);
    context.insert("schema", 1);
    context.insert("savedAt", savedAt.toString(Qt::ISODateWithMs));
    context.insert("imageWidth", frame.width());
    context.insert("imageHeight", frame.height());
    const auto crop = resultTableRect();
    context.insert("resultCrop", QJsonObject{{"x", crop.x()},
                                             {"y", crop.y()},
                                             {"width", crop.width()},
                                             {"height", crop.height()}});
    result.files = context;
    if (!savePng(base + ".png", frame, result.error))
      return;
    result.files.insert("fullFrame", base + ".png");
    if (!savePng(base + "-result.png", frame.copy(crop), result.error))
      return;
    result.files.insert("resultTable", base + "-result.png");
    result.files.insert("metadata", base + ".json");
    const auto bytes = QJsonDocument(result.files).toJson();
    QSaveFile metadata(base + ".json");
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(bytes) != bytes.size() ||
        !metadata.commit()) {
      result.files.remove("metadata");
      result.error = QStringLiteral("リザルト画像の情報を保存できません: %1 (%2)")
                         .arg(base + ".json", metadata.errorString());
    }
  };
  save();
  result.durationMs = elapsed.elapsed();
  return result;
}
} // namespace quickscore
