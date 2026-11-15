#include "result-capture.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir profile;
    require(profile.isValid(), "temporary profile");
    QImage frame(1920, 1080, QImage::Format_ARGB32);
    for (int y = 0; y < frame.height(); ++y)
      for (int x = 0; x < frame.width(); ++x)
        frame.setPixel(x, y, qRgb(x % 256, y % 256, (x + y) % 256));
    const auto original = frame.copy();
    const auto directory = profile.path() + "/captures";
    const auto first = quickscore::saveResultCapture(
        directory, frame,
        QJsonObject{{"source", "Game fixture"}, {"frameReceivedAt", "2026-10-04T12:34:56.789Z"}});
    require(first.error.isEmpty(), "save full frame, crop and metadata");
    frame.fill(Qt::black);
    const auto full =
        QImage(first.files["fullFrame"].toString()).convertToFormat(QImage::Format_ARGB32);
    const auto crop =
        QImage(first.files["resultTable"].toString()).convertToFormat(QImage::Format_ARGB32);
    require(full == original, "PNG preserves the frozen frame after live input changes");
    require(crop == original.copy(quickscore::resultTableRect()),
            "crop preserves all pixels at the detector coordinates");
    require(quickscore::resultTableRect().contains(QPoint(830, 77)) &&
                quickscore::resultTableRect().contains(QPoint(1843, 1008)),
            "crop includes all twelve ranks, names and score columns");
    QFile metadata(first.files["metadata"].toString());
    require(metadata.open(QIODevice::ReadOnly), "read capture metadata");
    const auto recorded = QJsonDocument::fromJson(metadata.readAll()).object();
    require(recorded == first.files && recorded["source"] == "Game fixture" &&
                recorded["frameReceivedAt"] == "2026-10-04T12:34:56.789Z",
            "metadata identifies the exact saved frame and source");
    const auto second = quickscore::saveResultCapture(directory, frame, {});
    require(second.error.isEmpty() && second.files["fullFrame"] != first.files["fullFrame"] &&
                QDir(directory).entryList({"*.png"}, QDir::Files).size() == 4,
            "separate captures never overwrite previous evidence");
    QFile blocker(profile.path() + "/blocked");
    require(blocker.open(QIODevice::WriteOnly), "create blocked directory fixture");
    blocker.close();
    const auto failed =
        quickscore::saveResultCapture(blocker.fileName() + "/captures", original, {});
    require(!failed.error.isEmpty() && !failed.files.contains("fullFrame"),
            "storage failure is explicit and never reports a saved image");
    require(!quickscore::saveResultCapture(directory, QImage(), {}).error.isEmpty(),
            "empty frame is rejected");
    std::cout << "PASS frozen PNG, exact table crop, metadata, unique files, storage failure\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
