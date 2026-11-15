#include "local-reader.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace quickscore;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Analysis capture() {
  Analysis a;
  a.image = QImage(1920, 1080, QImage::Format_RGB32);
  a.image.fill(Qt::black);
  a.layout.anchors = a.layout.pluses = 12;
  return a;
}
QJsonObject response() {
  QJsonArray rows;
  for (int rank = 1; rank <= 12; ++rank)
    rows.append(QJsonObject{
        {"rank", rank},
        {"choice", rank <= 6 ? 0 : 1},
        {"readable", true},
        {"probabilities", rank <= 6 ? QJsonArray{.96, .02, .02} : QJsonArray{.02, .96, .02}}});
  return {{"schema", 1}, {"model", "visual-jev-4b-nf4"}, {"rows", rows}};
}
void replace(QJsonObject &root, int row, QJsonObject value) {
  auto rows = root["rows"].toArray();
  rows[row] = value;
  root["rows"] = rows;
}
void pumpUntil(const std::function<bool()> &done) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 3000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  require(done(), "asynchronous worker timeout");
}
void write(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  require(f.open(QIODevice::WriteOnly), "write fixture");
  f.write(bytes);
}
int mockWorker(const QStringList &args) {
  if (!qEnvironmentVariableIsEmpty("GEMINI_API_KEY") ||
      qEnvironmentVariable("HF_HUB_OFFLINE") != "1")
    return 3;
  QFile config(args.last());
  config.open(QIODevice::ReadOnly);
  const auto options = QJsonDocument::fromJson(config.readAll()).object();
  std::cout << "{\"event\":\"ready\",\"schema\":1}\n" << std::flush;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
    if (options.value("delay").toBool())
      QThread::msleep(500);
    auto reply = response();
    reply["id"] = options.value("wrongId").toBool() ? "wrong-id" : request["id"];
    std::cout << QJsonDocument(reply).toJson(QJsonDocument::Compact).constData() << '\n'
              << std::flush;
  }
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("-u"))
    return mockWorker(app.arguments());
  try {
    Session session;
    session.configure({"A", "B"}, 6);
    auto reply = response();
    require(session.validate(parseLocalReply(reply, capture(), {"A", "B"}).standings).ok,
            "strong candidate scores produce valid standings");
    auto row = reply["rows"].toArray()[0].toObject();
    row["readable"] = false;
    replace(reply, 0, row);
    auto result = parseLocalReply(reply, capture(), {"A", "B"});
    require(result.standings[0].tag.empty() && !session.validate(result.standings).ok,
            "destroyed evidence is never filled by team capacity");
    row["readable"] = true;
    row["probabilities"] = QJsonArray{.55, .44, .01};
    replace(reply, 0, row);
    require(parseLocalReply(reply, capture(), {"A", "B"}).standings[0].tag.empty(),
            "ambiguous first character remains unknown");
    row["choice"] = -1;
    row["probabilities"] = QJsonArray{.01, .01, .98};
    replace(reply, 0, row);
    require(parseLocalReply(reply, capture(), {"A", "B"}).standings[0].tag.empty(),
            "unknown is an explicit candidate");
    row["choice"] = 0;
    row["probabilities"] = QJsonArray{.01, .98, .01};
    replace(reply, 0, row);
    require(!parseLocalReply(reply, capture(), {"A", "B"}).error.isEmpty(),
            "selected ID must agree with candidate scores");
    row["probabilities"] = QJsonArray{.98, .98, .01};
    replace(reply, 0, row);
    require(!parseLocalReply(reply, capture(), {"A", "B"}).error.isEmpty(),
            "probabilities must sum to one");
    reply = response();
    row = reply["rows"].toArray()[0].toObject();
    row["rank"] = 2;
    replace(reply, 0, row);
    require(!parseLocalReply(reply, capture(), {"A", "B"}).error.isEmpty(),
            "duplicate rank rejected");
    require(!parseLocalReply(response(), capture(), {"A", "a"}).error.isEmpty(),
            "duplicate normalized initial rejected");
    QTemporaryDir temp;
    require(temp.isValid(), "isolated worker fixture");
    write(temp.path() + "/config.json", "{}");
    write(temp.path() + "/adapter_config.json", "{}");
    write(temp.path() + "/worker.py", "# mock marker");
    auto config = QJsonObject{{"schema", 1},
                              {"pythonPath", app.applicationFilePath()},
                              {"modelDirectory", temp.path()},
                              {"adapterDirectory", temp.path()}};
    const auto path = temp.path() + "/local-runtime.json";
    write(path, QJsonDocument(config).toJson());
    qputenv("GEMINI_API_KEY", "must-not-enter-local-worker");
    LocalReader worker(path, temp.path() + "/worker.py");
    QString error;
    bool warmed = false;
    worker.prepare([&](QString e) {
      error = e;
      warmed = true;
    });
    pumpUntil([&] { return warmed; });
    require(error.isEmpty() && worker.ready(), "managed worker readiness");
    bool done = false;
    worker.read(capture(), {"A", "B"}, [&](Analysis a) {
      result = std::move(a);
      done = true;
    });
    pumpUntil([&] { return done; });
    require(session.validate(result.standings).ok && worker.ready() && !worker.busy(),
            "stdio transport leaves model resident for next race");
    worker.cancel();
    require(!worker.ready() && !worker.busy(), "stop frees owned worker");
    config["delay"] = true;
    write(path, QJsonDocument(config).toJson());
    warmed = false;
    worker.prepare([&](QString e) {
      error = e;
      warmed = true;
    });
    pumpUntil([&] { return warmed; });
    int callbacks = 0;
    worker.read(capture(), {"A", "B"}, [&](Analysis) { ++callbacks; });
    worker.cancel();
    require(callbacks == 0 && !worker.ready(), "cancel discards pending result");
    config["wrongId"] = true;
    config["delay"] = false;
    write(path, QJsonDocument(config).toJson());
    warmed = false;
    worker.prepare([&](QString e) {
      error = e;
      warmed = true;
    });
    pumpUntil([&] { return warmed; });
    done = false;
    worker.read(capture(), {"A", "B"}, [&](Analysis a) {
      result = std::move(a);
      done = true;
    });
    pumpUntil([&] { return done; });
    require(!result.error.isEmpty() && !worker.ready(), "stale worker answer rejected");
    std::cout << "PASS candidate validation, unknown rows, managed offline worker, cancellation\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
