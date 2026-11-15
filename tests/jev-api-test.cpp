#include "credential-store.hpp"
#include "jev-api-reader.hpp"
#include "mock-http.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace quickscore;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Credential {
  QString profile;
  ~Credential() {
    QString error;
    saveStoredCredential(profile, "JevApi", {}, error);
  }
  void set(QString key) {
    QString error;
    require(saveStoredCredential(profile, "JevApi", key, error), "credential storage");
  }
};
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--store-key") {
      QFile file(args[2]);
      require(file.open(QIODevice::ReadOnly) && file.size() < 8192, "key file");
      auto secret = QString::fromUtf8(file.readAll()).remove(QChar(0xfeff)).trimmed();
      require(!secret.isEmpty() && !secret.contains('\n') && !secret.contains('\r'),
              "single API key required");
      QString error;
      require(saveStoredCredential(args[3], "JevApi", secret, error), "save API credential");
      std::cout << "Stored Jev API key in Windows Credential Manager.\n";
      return 0;
    }
    QTemporaryDir temp;
    require(temp.isValid(), "temporary profile");
    Credential credential{temp.path()};
    std::vector<std::string> tags{"d", "T", "た", "A", "F", "C"};
    const int truth[]{0, 1, 0, 2, 3, 4, 3, 4, 5, 5, 1, 2};
    Analysis captured;
    captured.image = QImage(1920, 1080, QImage::Format_ARGB32);
    captured.image.fill(Qt::black);
    captured.layout.anchors = captured.layout.pluses = 12;
    auto wait = [&](const std::function<bool()> &finished, int ms) {
      QElapsedTimer t;
      t.start();
      while (!finished() && t.elapsed() < ms) {
        app.processEvents();
        QThread::msleep(5);
      }
      require(finished(), "asynchronous completion timeout");
    };
    if (args.size() == 2 && args[1] == "--self-test") {
      credential.set("fake-test-token");
      QJsonObject answers;
      for (int r = 0; r < 12; ++r) {
        QJsonObject probabilities;
        for (int i = 0; i < 6; ++i)
          probabilities.insert(QString("team%1").arg(i), i == truth[r] ? .98 : .02 / 6);
        probabilities.insert("unknown", .02 / 6);
        answers.insert(QString("rank%1").arg(r + 1),
                       QJsonObject{{"type", "choice"},
                                   {"choice", QString("team%1").arg(truth[r])},
                                   {"confidence", .99},
                                   {"probabilities", probabilities}});
      }
      QJsonObject response{{"model", "jev-1.14.0"},
                           {"answers", answers},
                           {"usage", QJsonObject{{"input_tokens", 100}}}};
      auto parsed = parseJevResponse(response, captured, tags);
      require(parsed.error.isEmpty() && parsed.standings.size() == 12, "structured response");
      for (int r = 0; r < 12; ++r)
        require(parsed.standings[r].tag == tags[truth[r]], "rank and tag mapping");
      auto broken = response;
      auto ba = answers;
      ba.remove("rank12");
      broken["answers"] = ba;
      require(!parseJevResponse(broken, captured, tags).error.isEmpty(), "missing row rejected");
      ba = answers;
      auto weak = ba["rank1"].toObject();
      weak["confidence"] = .2;
      ba["rank1"] = weak;
      broken = response;
      broken["answers"] = ba;
      require(parseJevResponse(broken, captured, tags).standings[0].tag.empty(),
              "weak answer held without quota filling");
      weak["probabilities"] = QJsonObject{{"team0", 2.0}};
      ba["rank1"] = weak;
      broken["answers"] = ba;
      require(!parseJevResponse(broken, captured, tags).error.isEmpty(),
              "invalid probabilities rejected");
      require(jevMontage(captured.image).size() == QSize(480, 448),
              "vision dimensions within official limits");
      const auto request = jevRequest(tags, "unique-image-reference");
      require(request["questions"].toObject().size() == 12 &&
                  request["assets"].toObject().size() == 1,
              "one asset and twelve parallel questions");
      MockServer mock;
      mock.body = QJsonDocument(response).toJson();
      JevApiReader reader(temp.path(), mock.url());
      bool done = false;
      reader.read(captured, tags, [&](Analysis a) {
        require(a.error.isEmpty(), "mock response");
        done = true;
      });
      reader.read(captured, tags, [&](Analysis) { throw std::runtime_error("duplicate request"); });
      wait([&] { return done; }, 3000);
      require(mock.count == 1 &&
                  mock.request.toLower().contains("typesafe-danger-zone-images: true") &&
                  mock.request.contains("multipart/form-data") &&
                  mock.request.contains("name=\"request\"") &&
                  mock.request.contains("name=\"montage\""),
              "multipart request and image header");
      mock.status = 429;
      done = false;
      reader.read(captured, tags, [&](Analysis a) {
        require(!a.error.isEmpty() && a.standings[0].tag.empty(), "rate limit holds result");
        done = true;
      });
      wait([&] { return done; }, 3000);
      require(mock.count == 2, "no automatic retries");
      mock.status = 200;
      mock.delay = 100;
      bool cancelledCallback = false;
      reader.read(captured, tags, [&](Analysis) { cancelledCallback = true; });
      reader.cancel();
      QElapsedTimer t;
      t.start();
      while (t.elapsed() < 250) {
        app.processEvents();
        QThread::msleep(5);
      }
      require(!cancelledCallback && !reader.busy(), "stop discards late reply");
      std::cout << "PASS Jev API mapping, unknown hold, malformed reply, multipart, duplicate, "
                   "rate limit, cancellation\n";
      return 0;
    }
    require(args.size() == 5, "jev-api-test image labels.json keyfile report.json");
    QFile keyFile(args[3]);
    require(keyFile.open(QIODevice::ReadOnly) && keyFile.size() < 8192, "key file");
    auto key = QString::fromUtf8(keyFile.readAll()).remove(QChar(0xfeff)).trimmed();
    require(!key.isEmpty() && !key.contains('\n') && !key.contains('\r'),
            "single API key required");
    credential.set(key);
    key.clear();
    QFile labels(args[2]);
    require(labels.open(QIODevice::ReadOnly), "labels file");
    const auto labeled = QJsonDocument::fromJson(labels.readAll()).object();
    tags.clear();
    for (const auto &tag : labeled["initials"].toArray())
      tags.push_back(tag.toString().toStdString());
    captured.image = QImage(args[1]).scaled(1920, 1080).convertToFormat(QImage::Format_ARGB32);
    require(!captured.image.isNull(), "source screenshot");
    require(jevMontage(captured.image).save(args[4] + ".montage.png"), "save transmitted montage");
    JevApiReader reader(temp.path());
    bool done = false;
    Analysis result;
    QElapsedTimer elapsed;
    elapsed.start();
    reader.read(captured, tags, [&](Analysis a) {
      result = std::move(a);
      done = true;
    });
    wait([&] { return done; }, 18000);
    int correct = 0, held = 0, wrong = 0;
    QJsonArray rows;
    const auto expected = labeled["truth"].toArray();
    for (int r = 0; r < 12; ++r) {
      const auto &s = result.standings[r];
      const auto wanted = tags[expected[r].toInt()];
      if (s.tag == wanted)
        ++correct;
      else if (s.tag.empty())
        ++held;
      else
        ++wrong;
      rows.append(QJsonObject{{"rank", r + 1},
                              {"tag", QString::fromStdString(s.tag)},
                              {"expected", QString::fromStdString(wanted)},
                              {"confidence", s.confidence},
                              {"evidence", result.evidence[r].text}});
    }
    QJsonObject report{{"model", reader.lastModel},
                       {"usage", reader.lastUsage},
                       {"elapsed_ms", elapsed.elapsed()},
                       {"correct", correct},
                       {"held", held},
                       {"wrong", wrong},
                       {"error", result.error},
                       {"rows", rows}};
    QFile output(args[4]);
    require(output.open(QIODevice::WriteOnly), "report file");
    output.write(QJsonDocument(report).toJson());
    std::cout << QJsonDocument(QJsonObject{{"correct", correct},
                                           {"held", held},
                                           {"wrong", wrong},
                                           {"ms", elapsed.elapsed()},
                                           {"error", result.error}})
                     .toJson()
                     .constData();
    return result.error.isEmpty() ? 0 : 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
