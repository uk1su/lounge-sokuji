#include "fast-reader.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <iostream>
#include <stdexcept>

using namespace quickscore;
static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static Analysis layout(QImage image) {
  Analysis result;
  result.image = image.scaled(1920, 1080).convertToFormat(QImage::Format_ARGB32);
  result.layout.anchors = result.layout.pluses = 12;
  return result;
}
static std::vector<Standing> labels(const QJsonObject &entry) {
  std::vector<Standing> result;
  const auto tags = entry["initials"].toArray();
  int rank = 1;
  for (const auto &choice : entry["truth"].toArray()) {
    const int index = choice.toInt();
    result.push_back({rank++, index < 0 ? std::string{} : tags[index].toString().toStdString(), 1});
  }
  return result;
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    const auto arguments = app.arguments();
    if (arguments.size() == 2 && arguments[1] == "--self-test") {
      QImage image(1920, 1080, QImage::Format_ARGB32);
      image.fill(Qt::black);
      std::vector<Standing> confirmed;
      for (int row = 0; row < 12; ++row) {
        confirmed.push_back({row + 1, row % 2 ? "B" : "A", 1});
        for (int y = 0; y < 40; ++y)
          for (int x = 0; x < 72; ++x)
            image.setPixel(1018 + x, 90 + 78 * row + y,
                           qRgb((x * (row % 2 ? 37 : 13) + y * 19 + x * y * 7) % 256,
                                (x * (row % 2 ? 37 : 13) + y * 19 + x * y * 7) % 256,
                                (x * (row % 2 ? 37 : 13) + y * 19 + x * y * 7) % 256));
      }
      const auto samples = confirmedSamples(image, confirmed);
      require(samples.size() == 12, "twelve manually confirmed samples");
      const auto read = readFast(layout(image), {"A", "B"}, samples);
      for (int row = 0; row < 12; ++row)
        require(read.standings[size_t(row)].tag == confirmed[size_t(row)].tag,
                "arbitrary own tag A without an ITF template");
      auto unknown = image.copy();
      QPainter painter(&unknown);
      painter.fillRect(QRect(1010, 80, 100, 70), Qt::black);
      painter.end();
      require(readFast(layout(unknown), {"A", "B"}, samples).standings[0].tag.empty(),
              "hidden initial is not filled by team capacity");
      auto ambiguous = samples;
      for (auto &sample : ambiguous)
        if (sample.tag == "A") {
          auto same = sample;
          same.tag = "B";
          ambiguous.push_back(same);
          break;
        }
      require(readFast(layout(image), {"A", "B"}, ambiguous).standings[0].tag.empty(),
              "identical competing templates are held");
      require(readFast(layout(image), {"A", "B"}, {}).standings[0].tag.empty(),
              "first result requires confirmation");
      std::cout << "PASS confirmed templates, arbitrary A, unknown and ambiguous held\n";
      return 0;
    }
    require(arguments.size() == 3, "fast-test dataset.json report.json");
    QFile file(arguments[1]);
    require(file.open(QIODevice::ReadOnly), "read labeled image dataset");
    const auto cases = QJsonDocument::fromJson(file.readAll()).array();
    require(!cases.isEmpty(), "image cases");
    const auto calibration = cases[0].toObject();
    QJsonArray report;
    for (const auto &entry : cases) {
      const auto fixture = entry.toObject();
      const auto frame = layout(QImage(fixture["image"].toString()));
      require(!frame.image.isNull(), "fixture image decoder or file unavailable");
      const auto truth = labels(fixture);
      std::vector<std::string> tags;
      for (const auto &tag : fixture["initials"].toArray())
        tags.push_back(tag.toString().toStdString());
      const bool twoVTwoBank =
          fixture["id"].toString() == "provided-2v2" || fixture["kind"].toString() != "real";
      const auto selectedCalibration = fixture.contains("calibration")
                                           ? fixture["calibration"].toObject()
                                           : (twoVTwoBank ? calibration : fixture);
      const auto calibrationFrame = layout(QImage(selectedCalibration["image"].toString()));
      require(!calibrationFrame.image.isNull(), "calibration image unavailable");
      const auto samples = confirmedSamples(calibrationFrame.image, labels(selectedCalibration));
      QElapsedTimer elapsed;
      elapsed.start();
      const auto read = readFast(frame, tags, samples);
      const auto microseconds = elapsed.nsecsElapsed() / 1000;
      int correct = 0, falseMatches = 0, held = 0;
      QJsonArray rows;
      for (size_t row = 0; row < read.standings.size(); ++row) {
        const auto &standing = read.standings[row];
        if (standing.tag == truth[row].tag)
          ++correct;
        else if (standing.tag.empty())
          ++held;
        else
          ++falseMatches;
        rows.append(QJsonObject{{"rank", int(row + 1)},
                                {"tag", QString::fromStdString(standing.tag)},
                                {"expected", QString::fromStdString(truth[row].tag)},
                                {"confidence", standing.confidence},
                                {"best", QString::fromStdString(read.evidence[row].closestTag)},
                                {"similarity", read.evidence[row].similarity}});
      }
      report.append(QJsonObject{{"id", fixture["id"]},
                                {"kind", fixture["kind"]},
                                {"calibration_image", selectedCalibration["image"]},
                                {"correct", correct},
                                {"held", held},
                                {"false", falseMatches},
                                {"microseconds", microseconds},
                                {"rows", rows}});
    }
    QFile output(arguments[2]);
    require(output.open(QIODevice::WriteOnly), "write actual-image report");
    output.write(QJsonDocument(report).toJson());
    std::cout << "Completed " << report.size() << " labeled-image cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
