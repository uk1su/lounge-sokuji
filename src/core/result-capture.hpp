#pragma once
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>

namespace quickscore {
struct ResultCapture {
  QJsonObject files;
  QString error;
  qint64 durationMs{};
};
// Coordinates in the same 1920x1080 frame used by the layout detector and Jev.
QRect resultTableRect();
void pruneResultCaptures(const QString &directory, int maxKeep = 60);
ResultCapture saveResultCapture(const QString &directory, const QImage &frame, QJsonObject context);
} // namespace quickscore
