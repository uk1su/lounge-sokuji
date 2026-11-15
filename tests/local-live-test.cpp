#include "local-reader.hpp"
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>
using namespace quickscore;
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 6) {
    std::cerr << "local-live-test templates image comma-tags runtime worker\n";
    return 2;
  }
  std::vector<std::string> tags;
  for (const auto &tag : args[3].split(','))
    tags.push_back(tag.trimmed().toStdString());
  Analyzer analyzer(args[1]);
  auto capture = analyzer.read(QImage(args[2]), tags, {}, false, true);
  if (!capture.complete()) {
    std::cerr << "Result gate is incomplete\n";
    return 3;
  }
  Session session;
  session.configure(tags, 12 / int(tags.size()));
  LocalReader reader(args[4], args[5]);
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer::singleShot(180000, &app, [&] {
    reader.cancel();
    app.exit(4);
  });
  int resultCode = 1;
  reader.prepare([&](QString error) {
    if (!error.isEmpty()) {
      std::cerr << error.toStdString() << '\n';
      app.exit(1);
      return;
    }
    const auto load = elapsed.elapsed();
    elapsed.restart();
    reader.read(capture, tags, [&, load](Analysis result) {
      const auto validation = session.validate(result.standings);
      QJsonArray rows;
      for (const auto &standing : result.standings)
        rows.append(QJsonObject{{"rank", standing.rank},
                                {"tag", QString::fromStdString(standing.tag)},
                                {"score", standing.confidence}});
      if (validation.ok)
        session.commit(result.standings, false);
      const auto report = QJsonObject{
          {"ready_ms", load},
          {"read_ms", elapsed.elapsed()},
          {"error", result.error},
          {"valid", validation.ok},
          {"rows", rows},
          {"scoreLine", validation.ok ? formatScoreLine(tags, session.totals(), 1) : QString{}}};
      std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
      resultCode = validation.ok ? 0 : 1;
      app.exit(resultCode);
    });
  });
  return app.exec();
}
