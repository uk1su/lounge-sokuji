#include "credential-store.hpp"
#include "main-dock.hpp"
#include "mock-http.hpp"
#include "result-capture.hpp"
#include "scoreboard-source.hpp"
#include "session-store.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <obs-module.h>
#include <stdexcept>
#include <windows.h>
#pragma warning(disable : 4996)
static obs_module_t *module = nullptr;
extern "C" obs_module_t *obs_current_module(void) { return module; }
extern "C" const char *obs_module_text(const char *s) { return s; }
static const char *name(void *) { return "Game fixture"; }
static void *create(obs_data_t *, obs_source_t *source) { return source; }
static void destroy(void *) {}
static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 7 && args.size() != 8) {
    std::cerr << "dock-test obs-directory plugin-dll data image local-runtime preview.png "
                 "[labels.json]\n";
    return 2;
  }
  try {
    const bool live = args.size() == 8;
    QStringList teamTags{"M", QStringLiteral("う"), "A", "L", "B", "R"};
    QStringList expected{"M", QStringLiteral("う"), "A", "L", "B", "L",
                         "R", QStringLiteral("う"), "A", "R", "M", "B"};
    const auto dllDirectory = (args[1] + "/bin/64bit").toStdWString();
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    AddDllDirectory(dllDirectory.c_str());
    QTemporaryDir config;
    require(config.isValid(), "isolated test profile");
    require(obs_startup("ja-JP", config.path().toUtf8().constData(), nullptr), "OBS startup");
    obs_add_data_path((args[1] + "/data/libobs/").toUtf8().constData());
    obs_video_info v{};
    const auto graphics = (args[1] + "/bin/64bit/libobs-d3d11.dll").toUtf8();
    v.graphics_module = graphics.constData();
    v.fps_num = 30;
    v.fps_den = 1;
    v.base_width = v.output_width = 1920;
    v.base_height = v.output_height = 1080;
    v.output_format = VIDEO_FORMAT_NV12;
    v.colorspace = VIDEO_CS_709;
    v.range = VIDEO_RANGE_PARTIAL;
    v.scale_type = OBS_SCALE_BILINEAR;
    require(obs_reset_video(&v) == OBS_VIDEO_SUCCESS, "OBS D3D video startup");
    require(obs_open_module(&module, args[2].toUtf8().constData(), args[3].toUtf8().constData()) ==
                MODULE_SUCCESS,
            "built plugin DLL load");
    quickscore::registerScoreboardSource();
    obs_source_info info{};
    info.id = "quickscore_fixture";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    info.get_name = name;
    info.create = create;
    info.destroy = destroy;
    obs_register_source(&info);
    auto *source = obs_source_create("quickscore_fixture", "Game fixture", nullptr, nullptr);
    require(source != nullptr, "fixture source");
    obs_set_output_source(0, source);
    auto image = QImage(args[4]).scaled(1920, 1080).convertToFormat(QImage::Format_ARGB32);
    require(!image.isNull(), "2v2 screenshot load");
    require(QFile::copy(args[5], config.path() + "/local-runtime.json"),
            "real runtime configuration");
    {
      QSettings settings(config.path() + "/settings.ini", QSettings::IniFormat);
      settings.setValue("schema", 11);
      settings.setValue("sourceKey", "Game fixture");
      settings.setValue("tags", teamTags.join(", "));
    }
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(35, 39, 47));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(25, 29, 35));
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(50, 55, 65));
    palette.setColor(QPalette::ButtonText, Qt::white);
    app.setPalette(palette);
    app.setStyle("Fusion");
    MockServer discordApi, jevApi;
    discordApi.body = "{\"ok\":true,\"posted\":true}";
    QJsonObject answers;
    const int choices[]{0, 1, 2, 3, 4, 3, 5, 1, 2, 5, 0, 4};
    for (int r = 0; r < 12; ++r) {
      QJsonObject probabilities;
      for (int i = 0; i < 6; ++i)
        probabilities.insert(QString("team%1").arg(i), i == choices[r] ? .98 : .02 / 6);
      probabilities.insert("unknown", .02 / 6);
      answers.insert(QString("rank%1").arg(r + 1),
                     QJsonObject{{"type", "choice"},
                                 {"choice", QString("team%1").arg(choices[r])},
                                 {"confidence", .99},
                                 {"probabilities", probabilities}});
    }
    const QByteArray normalBody = QJsonDocument(QJsonObject{{"model", "jev-1.14.0"},
                                                            {"answers", answers},
                                                            {"usage", QJsonObject{{"input_tokens", 1200}}}})
                                      .toJson();
    QJsonObject unknownAnswers = answers;
    {
      QJsonObject probs;
      for (int i = 0; i < 6; ++i)
        probs.insert(QString("team%1").arg(i), 0.02 / 6);
      probs.insert("unknown", 0.98);
      unknownAnswers.insert("rank1", QJsonObject{{"type", "choice"},
                                                 {"choice", "unknown"},
                                                 {"confidence", .98},
                                                 {"probabilities", probs}});
      unknownAnswers.insert("rank2", QJsonObject{{"type", "choice"},
                                                 {"choice", "unknown"},
                                                 {"confidence", .98},
                                                 {"probabilities", probs}});
    }
    const QByteArray unknownRow0Body =
        QJsonDocument(QJsonObject{{"model", "jev-1.14.0"},
                                  {"answers", unknownAnswers},
                                  {"usage", QJsonObject{{"input_tokens", 1200}}}})
            .toJson();
    jevApi.body = normalBody;
    QString credentialError;
    QString token = "mock-api-key";
    if (live) {
      QFile f(args[7]);
      require(f.open(QIODevice::ReadOnly) && f.size() < 8192, "key file");
      token = QString::fromUtf8(f.readAll()).remove(QChar(0xfeff)).trimmed();
      require(!token.isEmpty() && !token.contains('\n'), "single key");
    }
    require(quickscore::saveStoredCredential(config.path(), "JevApi", token, credentialError),
            "test credential");
    token.clear();
    auto dock = std::make_unique<quickscore::MainDock>(config.path(), nullptr, discordApi.url(),
                                                       live ? QUrl{} : jevApi.url());
    dock->setAttribute(Qt::WA_DontShowOnScreen);
    dock->resize(280, 560);
    dock->show();
    std::cout << "COMPACT minHint=" << dock->minimumSizeHint().width() << "x"
              << dock->minimumSizeHint().height() << " hint=" << dock->sizeHint().width() << "x"
              << dock->sizeHint().height() << "\n";
    require(dock->minimumSizeHint().width() <= 260, "compact horizontal dock minimum width");
    auto *start = dock->findChild<QPushButton *>("start");
    auto *stop = dock->findChild<QPushButton *>("stop");
    auto *reset = dock->findChild<QPushButton *>("reset");
    auto *scores = dock->findChild<QLabel *>("scores");
    auto *state = dock->findChild<QLabel *>("status");
    auto *preview = dock->findChild<quickscore::ResultPreviewWidget *>("preview");
    auto *postDiscord = dock->findChild<QCheckBox *>("postDiscord");
    auto *raceHistory = dock->findChild<QComboBox *>("raceHistory");
    auto *editRace = dock->findChild<QPushButton *>("editRace");
    require(start && stop && reset && scores && state && preview && postDiscord && raceHistory &&
                editRace,
            "native widgets");
    require(start->isEnabled() && !stop->isEnabled(), "start/stop initial enabled state");
    require(!dock->findChild<QWidget *>("correction"), "duplicate 12-row bottom table removed");
    require(!dock->findChild<QWidget *>("format"), "format selector removed (auto from tag count)");
    auto *detectedFormat = dock->findChild<QLabel *>("detectedFormat");
    require(detectedFormat && detectedFormat->text() == QStringLiteral("2v2"),
            "6 tags automatically detected as 2v2");
    require(!editRace->isEnabled(), "cannot edit before any race is recorded");
    require(!dock->findChild<QComboBox *>("recognitionMode") &&
                !dock->findChild<QLineEdit *>("geminiKey") &&
                !dock->findChild<QCheckBox *>("autoCommit") &&
                !dock->findChild<QCheckBox *>("jevApiEnabled") &&
                !dock->findChild<QLineEdit *>("jevApiKey") &&
                !dock->findChild<QPushButton *>("saveJevApiKey") &&
                !dock->findChild<QPushButton *>("clearFast") &&
                !dock->findChild<QPushButton *>("jevAssist") &&
                !dock->findChild<QLineEdit *>("discordToken") &&
                !dock->findChild<QLineEdit *>("discordChannel") &&
                !dock->findChild<QPushButton *>("discordSave") &&
                !dock->findChild<QPushButton *>("discordDelete") &&
                !dock->findChild<QPushButton *>("discordSend") &&
                !dock->findChild<QPushButton *>("undo") &&
                !dock->findChild<QPushButton *>("manual") &&
                !dock->findChild<QPushButton *>("captureFolder") &&
                !dock->findChild<QPushButton *>("diagnose") &&
                !dock->findChild<QLabel *>("captureStatus") &&
                !dock->findChild<QLabel *>("dockTitle"),
            "streamlined Lounge UI without removed settings controls");
    require(!postDiscord->isChecked(), "Discord auto-post defaults to unchecked");
    const QDir captures(config.path() + "/captures");
    auto captureFiles = [&] { return captures.entryList({"*.json"}, QDir::Files, QDir::Name); };
    require(captureFiles().isEmpty(), "no capture before a result");
    QImage blank(1920, 1080, QImage::Format_ARGB32);
    blank.fill(Qt::black);
    auto pump = [&](const QImage &im, int milliseconds) {
      QElapsedTimer timer;
      timer.start();
      while (timer.elapsed() < milliseconds) {
        obs_source_frame frame{};
        frame.data[0] = const_cast<uint8_t *>(im.constBits());
        frame.linesize[0] = uint32_t(im.bytesPerLine());
        frame.width = 1920;
        frame.height = 1080;
        frame.format = VIDEO_FORMAT_BGRA;
        frame.timestamp = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
        obs_source_output_video(source, &frame);
        app.processEvents();
        QThread::msleep(20);
      }
    };
    start->click();
    pump(blank, 3500);
    const auto firstLine = QStringLiteral(
        "【M】17／う 17 (±0)／L 16 (+1)／A 14 (+3)／B 9 (+8)／R 9 (+8)／現在1位／＠11");
    QElapsedTimer elapsed;
    elapsed.start();
    while (scores->text() != firstLine && !preview->isEditing() && elapsed.elapsed() < 15000)
      pump(image, 50);
    if (live) {
      if (preview->isEditing()) {
        for (int row = 0; row < 12; ++row)
          require(preview->rowTag(row) == expected[row], "twelve API rows");
        dock->findChild<QPushButton *>("confirm")->click();
      }
      require(scores->text() == firstLine && discordApi.count == 0,
              "live score format without Discord when unchecked");
      QFile report(args[6] + ".json");
      require(report.open(QIODevice::WriteOnly), "live report");
      report.write(QJsonDocument(QJsonObject{{"real_obs_capture", true},
                                             {"real_jev_api", true},
                                             {"end_to_end_ms", elapsed.elapsed()},
                                             {"scoreLine", firstLine},
                                             {"rows_correct", 12}})
                       .toJson());
      start->click();
      dock.reset();
      quickscore::saveStoredCredential(config.path(), "JevApi", {}, credentialError);
      obs_set_output_source(0, nullptr);
      obs_source_release(source);
      obs_shutdown();
      std::cout << "PASS real OBS frame to live Jev API to correct score\n";
      return 0;
    }
    if (!(scores->text() == firstLine && !preview->isEditing() && jevApi.count == 1 &&
          discordApi.count == 0)) {
      std::cerr << "DIAG status=" << state->text().toStdString()
                << " diag=" << dock->findChild<QLabel *>("diagnostic")->text().toStdString()
                << " scores=" << scores->text().toStdString()
                << " editing=" << preview->isEditing() << " jevCount=" << jevApi.count
                << " discordCount=" << discordApi.count << " elapsed=" << elapsed.elapsed() << '\n';
    }
    require(scores->text() == firstLine && !preview->isEditing() && jevApi.count == 1 &&
                discordApi.count == 0,
            "OBS to one API request auto-commits without Discord when unchecked");
    pump(image, 1000);
    require(jevApi.count == 1, "duplicate guard");
    pump(blank, 3500);
    jevApi.delay = 800;
    elapsed.restart();
    while (jevApi.count < 2 && elapsed.elapsed() < 10000)
      pump(image, 50);
    stop->click();
    pump(image, 1200);
    require(scores->text() == firstLine && !preview->isEditing(), "stop cancels late API result");
    jevApi.delay = 0;
    postDiscord->click();
    elapsed.restart();
    while (discordApi.count < 1 && elapsed.elapsed() < 5000)
      pump(blank, 50);
    require(discordApi.count == 1, "checking postDiscord posts current score to discord_bot");
    const auto firstDiscordJson =
        QJsonDocument::fromJson(discordApi.request.mid(discordApi.request.indexOf("\r\n\r\n") + 4))
            .object();
    require(firstDiscordJson["score_line"].toString() == firstLine &&
                firstDiscordJson["post_to_discord"].toBool() &&
                !firstDiscordJson["active"].toBool() &&
                firstDiscordJson["channel_id"].toString() == QStringLiteral("1571454531814952971"),
            "discord_bot payload contains score_line, post_to_discord, active, and lounge channel_id");
    start->click();
    elapsed.restart();
    while (discordApi.count < 2 && elapsed.elapsed() < 5000)
      pump(blank, 50);
    require(discordApi.count == 2, "start sends overlay active=true without posting to Discord");
    const auto startDiscordJson =
        QJsonDocument::fromJson(discordApi.request.mid(discordApi.request.indexOf("\r\n\r\n") + 4))
            .object();
    require(startDiscordJson["active"].toBool() && !startDiscordJson["post_to_discord"].toBool(),
            "start payload activates overlay with post_to_discord=false");
    pump(blank, 3500);
    elapsed.restart();
    const auto secondLine = QStringLiteral(
        "【M】34／う 34 (±0)／L 32 (+2)／A 28 (+6)／B 18 (+16)／R 18 (+16)／現在1位／＠10");
    while ((scores->text() != secondLine || discordApi.count < 3) && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(scores->text() == secondLine && jevApi.count == 3 && discordApi.count == 3,
            "validated API auto-commits and auto-posts to Discord");
    const auto automaticMs = elapsed.elapsed();

    pump(blank, 3500);
    jevApi.body = unknownRow0Body;
    elapsed.restart();
    while (!preview->isEditing() && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(preview->isEditing() && preview->rowTag(0).isEmpty() && preview->rowTag(1).isEmpty() &&
                jevApi.count == 4 && discordApi.count == 3,
            "unknown player opens clickable badge review without posting to Discord");
    preview->setRowTag(0, QStringLiteral("M"));
    preview->setRowTag(1, QStringLiteral("う"));
    dock->findChild<QPushButton *>("confirm")->click();
    const auto thirdLine = QStringLiteral(
        "【M】51／う 51 (±0)／L 48 (+3)／A 42 (+9)／B 27 (+24)／R 27 (+24)／現在1位／＠9");
    elapsed.restart();
    while ((scores->text() != thirdLine || discordApi.count < 4) && elapsed.elapsed() < 5000)
      pump(blank, 50);
    require(scores->text() == thirdLine && !preview->isEditing() && discordApi.count == 4,
            "confirming manually assigned player updates score and persists override");

    pump(blank, 3500);
    elapsed.restart();
    const auto fourthLine = QStringLiteral(
        "【M】68／う 68 (±0)／L 64 (+4)／A 56 (+12)／B 36 (+32)／R 36 (+32)／現在1位／＠8");
    while ((scores->text() != fourthLine || discordApi.count < 5) && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(scores->text() == fourthLine && !preview->isEditing() && jevApi.count == 5 &&
                discordApi.count == 5,
            "subsequent race with Jev unknown auto-commits via learned player override");

    jevApi.body = normalBody;
    pump(blank, 3500);
    jevApi.status = 429;
    elapsed.restart();
    while (!preview->isEditing() && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(preview->isEditing() && scores->text() == fourthLine && jevApi.count == 6 &&
                discordApi.count == 5,
            "429 held without posting to Discord");
    pump(image, 1000);
    require(jevApi.count == 6, "no automatic retries");
    require(dock->grab().save(args[6]), "API UI preview");
    dock->findChild<QPushButton *>("cancel")->click();
    stop->click();
    elapsed.restart();
    while (discordApi.count < 6 && elapsed.elapsed() < 5000)
      pump(blank, 50);
    require(discordApi.count == 6, "manual stop syncs inactive overlay state");
    const auto stopDiscordJson =
        QJsonDocument::fromJson(discordApi.request.mid(discordApi.request.indexOf("\r\n\r\n") + 4))
            .object();
    require(!stopDiscordJson["active"].toBool() && !stopDiscordJson["post_to_discord"].toBool(),
            "stop payload deactivates overlay with post_to_discord=false");
    require(raceHistory->count() == 4 && editRace->isEnabled(), "four races in history");
    raceHistory->setCurrentIndex(0);
    editRace->click();
    require(preview->isEditing() && preview->hasPreview() && preview->hasTagStrip(),
            "editRace opens clickable right-side tag editor for 1R");
    require(dock->grab().save(args[6] + ".edit.png"), "edit UI preview with right-side tags");
    preview->setRowTag(0, QStringLiteral("う"));
    preview->setRowTag(1, QStringLiteral("M"));
    dock->findChild<QPushButton *>("confirm")->click();
    const auto editedLine = QStringLiteral(
        "う 71 (-6)／【M】65／L 64 (+1)／A 56 (+9)／B 36 (+29)／R 36 (+29)／現在2位／＠8");
    require(scores->text() == editedLine && !preview->isEditing(),
            "editing 1R updates total scores immediately");
    require(raceHistory->itemText(0).contains(QStringLiteral("補正済み")),
            "race history marks corrected race");
    elapsed.restart();
    while (discordApi.count < 7 && elapsed.elapsed() < 5000)
      pump(blank, 50);
    require(discordApi.count == 7, "editing 1R syncs corrected totals to Discord");
    require(dock->overlayPort() > 0, "overlay HTTP server bound to localhost port");
    require(QFile::exists(config.path() + "/overlay.html") &&
                QFile::exists(config.path() + "/overlay-state.json") &&
                QFile::exists(config.path() + "/overlay-state.js"),
            "overlay html and state files written to config directory");
    {
      QNetworkAccessManager nam;
      auto fetchLocal = [&](const QString &path) -> QByteArray {
        QNetworkReply *reply = nam.get(QNetworkRequest(
            QUrl(QString("http://127.0.0.1:%1%2").arg(dock->overlayPort()).arg(path))));
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!reply->isFinished() && waitTimer.elapsed() < 3000)
          app.processEvents(QEventLoop::AllEvents, 20);
        require(reply->isFinished() && reply->error() == QNetworkReply::NoError,
                "overlay HTTP fetch succeeded");
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        return body;
      };
      const QByteArray html = fetchLocal(QStringLiteral("/overlay.html"));
      require(html.contains("respect-kusaan") && html.contains("team-num-2"),
              "overlay HTTP serves mk8dx_sokuji overlay markup");
      const QJsonObject stateObj =
          QJsonDocument::fromJson(fetchLocal(QStringLiteral("/state.json"))).object();
      require(stateObj.value("team_count").toInt() == 6 &&
                  stateObj.value("remaining_races").toInt() == 8 &&
                  stateObj.value("teams_sorted").toArray().at(0).toObject().value("tag").toString() ==
                      QStringLiteral("う"),
              "overlay /state.json reflects live dock score order");
    }
    require(QDir(config.path() + "/captures").entryList({"*.json"}, QDir::Files).size() == 6,
            "exact images saved");
    {
      jevApi.status = 200;
      start->click();
      elapsed.restart();
      while (discordApi.count < 8 && elapsed.elapsed() < 5000)
        pump(blank, 50);
      raceHistory->setCurrentIndex(0);
      editRace->click();
      require(preview->isEditing(), "ExistingRace editor opens while aggregation is running");
      pump(blank, 3500);
      require(preview->isEditing(),
              "untouched ExistingRace editor stays open during non-result frames");
      const auto fifthLine = QStringLiteral(
          "う 91 (-12)／L 80 (-1)／【M】79／A 70 (+9)／B 45 (+34)／R 45 (+34)／現在3位／＠7");
      elapsed.restart();
      while ((scores->text() != fifthLine || preview->isEditing()) && elapsed.elapsed() < 10000)
        pump(image, 50);
      require(scores->text() == fifthLine && !preview->isEditing(),
              "background monitoring arms during ExistingRace edit and auto-commits new race");
      stop->click();
    }
    QFile report(args[6] + ".json");
    require(report.open(QIODevice::WriteOnly), "report");
    report.write(QJsonDocument(QJsonObject{{"real_obs_capture", true},
                                           {"mock_api_requests", jevApi.count},
                                           {"automatic_ms", automaticMs},
                                           {"scoreLine", firstLine},
                                           {"editedLine", editedLine},
                                           {"stop_late_reply_discarded", true},
                                           {"rate_limit_held", true}})
                     .toJson());
    dock.reset();
    quickscore::saveStoredCredential(config.path(), "JevApi", {}, credentialError);
    obs_set_output_source(0, nullptr);
    obs_source_release(source);
    obs_shutdown();
    std::cout << "PASS OBS capture to Jev API, manual/auto scoring, duplicate, cancellation, rate "
                 "limit, images\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
