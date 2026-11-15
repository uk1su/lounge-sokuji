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
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
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
    const bool manualLabels = args.size() == 8;
    QStringList teamTags{"M", QStringLiteral("う"), "A", "L", "B", "R"};
    QStringList expected{"M", QStringLiteral("う"), "A", "L", "B", "L",
                         "R", QStringLiteral("う"), "A", "R", "M", "B"};
    if (manualLabels) {
      QFile labels(args[7]);
      require(labels.open(QIODevice::ReadOnly), "manual calibration labels");
      const auto labeled = QJsonDocument::fromJson(labels.readAll()).object();
      teamTags.clear();
      expected.clear();
      for (const auto &tag : labeled["initials"].toArray())
        teamTags.append(tag.toString());
      for (const auto &tag : labeled["truth"].toArray())
        expected.append(teamTags[tag.toInt()]);
      require(teamTags.size() == 6 && expected.size() == 12, "new real 2v2 labels");
    }
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
    if (manualLabels)
      require(QFile::rename(config.path() + "/local-runtime.json",
                            config.path() + "/local-runtime.json.disabled"),
              "CPU test starts with no model configuration");
    {
      QSettings settings(config.path() + "/settings.ini", QSettings::IniFormat);
      settings.setValue("schema", 8);
      settings.setValue("sourceKey", "Game fixture");
      settings.setValue("format", 0);
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
    MockServer discordApi;
    discordApi.body = "{\"id\":\"123456789012345678\"}";
    auto dock = std::make_unique<quickscore::MainDock>(config.path(), nullptr, discordApi.url());
    dock->setAttribute(Qt::WA_DontShowOnScreen);
    dock->resize(760, 1200);
    dock->show();
    auto *start = dock->findChild<QPushButton *>("start");
    auto *scores = dock->findChild<QLabel *>("scores");
    auto *state = dock->findChild<QLabel *>("status");
    auto *table = dock->findChild<QTableWidget *>("correction");
    require(start && scores && state && table, "native widgets");
    require(!dock->findChild<QComboBox *>("recognitionMode") &&
                !dock->findChild<QLineEdit *>("geminiKey"),
            "Jev-only UI without cloud API fields");
    require(!dock->findChild<QCheckBox *>("autoCommit")->isChecked(),
            "default manual confirmation");
    const QDir captures(config.path() + "/captures");
    auto captureFiles = [&] { return captures.entryList({"*.json"}, QDir::Files, QDir::Name); };
    require(dock->findChild<QPushButton *>("captureFolder") && captureFiles().isEmpty(),
            "capture folder control exists; no capture before a result");
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
    QElapsedTimer wait;
    wait.start();
    while (state->text().contains(QStringLiteral("読み込み")) && wait.elapsed() < 120000)
      pump(blank, 100);
    require(start->text() == QStringLiteral("集計停止"), "fast mode starts without model loading");
    pump(blank, 3500);
    require(captureFiles().isEmpty(), "non-result screens are not saved");
    wait.restart();
    while (captureFiles().isEmpty() && wait.elapsed() < 30000)
      pump(image, 50);
    const auto firstCaptureMs = wait.elapsed();
    require(captureFiles().size() == 1 && table->isVisible(),
            "one exact result is saved before assistance or confirmation");
    QFile firstMetadata(captures.filePath(captureFiles().first()));
    require(firstMetadata.open(QIODevice::ReadOnly), "saved frame metadata");
    const auto firstCapture = QJsonDocument::fromJson(firstMetadata.readAll()).object();
    const auto firstFull =
        QImage(firstCapture["fullFrame"].toString()).convertToFormat(QImage::Format_ARGB32);
    const auto firstCrop =
        QImage(firstCapture["resultTable"].toString()).convertToFormat(QImage::Format_ARGB32);
    require(firstFull.size() == QSize(1920, 1080) &&
                firstCrop == firstFull.copy(quickscore::resultTableRect()),
            "real OBS capture saves full frame and exact twelve-row crop");
    const auto preserved = firstFull.copy();
    auto laterImage = image.copy();
    {
      QPainter painter(&laterImage);
      painter.fillRect(QRect(10, 160, 100, 100), QColor(255, 0, 255));
    }
    while (!table->isVisible() && wait.elapsed() < 30000)
      pump(laterImage, 50);
    const auto firstMs = wait.elapsed();
    std::cout << "First local status=" << state->text().toStdString() << "\n";
    require(table->isVisible(), "first result opens calibration review immediately");
    const auto *untrained = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
    require(untrained && untrained->currentData().toString().isEmpty(),
            "unregistered templates never guess tags");
    auto *assist = dock->findChild<QPushButton *>("jevAssist");
    require(assist && assist->isEnabled(), "optional Jev assistance during calibration");
    wait.restart();
    if (manualLabels) {
      for (int row = 0; row < 12; ++row) {
        auto *box = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
        const auto choice = box->findData(expected[row]);
        require(choice > 0, "manual tag choice available");
        box->setCurrentIndex(choice);
      }
      pump(laterImage, 100);
    } else {
      assist->click();
      while (!assist->isEnabled() && wait.elapsed() < 120000)
        pump(laterImage, 50);
      require(assist->isEnabled(), "real local Jev assistance completes");
    }
    const auto assistanceMs = manualLabels ? 0 : wait.elapsed();
    require(QImage(firstCapture["fullFrame"].toString()).convertToFormat(QImage::Format_ARGB32) ==
                    preserved &&
                preserved.pixelColor(40, 180) != laterImage.pixelColor(40, 180),
            "saved frame is unchanged when the live scene changes during inference");
    auto checkRows = [&] {
      for (int row = 0; row < 12; ++row) {
        const auto *box = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
        if (!box || box->currentData().toString() != expected[row])
          std::cout << "Rank " << row + 1
                    << " actual=" << (box ? box->currentText().toStdString() : "missing") << "\n";
        require(box && box->currentData().toString() == expected[row],
                "all twelve real 2v2 initials");
      }
    };
    checkRows();
    require(scores->text().contains(QStringLiteral("＠12")), "no score before confirmation");
    require(dock->grab().save(args[6] + ".review.png"), "review preview");
    dock->findChild<QPushButton *>("confirm")->click();
    std::vector<quickscore::TagSample> learned;
    QString learnedError;
    require(
        quickscore::loadSamples(config.path() + "/fast-templates.json", learned, learnedError) &&
            learned.size() == 12,
        "manual confirmation persists twelve trusted templates");
    const auto expectedLine =
        manualLabels
            ? QStringLiteral("【d】25／T 14 (+11)／A 14 (+11)／F 12 (+13)／た 10 (+15)／C 7 "
                             "(+18)／現在1位／＠11")
            : QStringLiteral(
                  "【M】17／う 17 (±0)／L 16 (+1)／A 14 (+3)／B 9 (+8)／R 9 (+8)／現在1位／＠11");
    require(scores->text() == expectedLine, "exact original score format and 82 points");
    QFile score(config.path() + "/score.txt");
    require(score.open(QIODevice::ReadOnly) && QString::fromUtf8(score.readAll()) == expectedLine,
            "score.txt matches without newline");
    score.close();
    pump(image, 1000);
    require(scores->text() == expectedLine, "same result never counted twice");
    require(captureFiles().size() == 1, "holding or confirming a result never saves duplicates");
    QFile session(config.path() + "/session.json");
    require(session.open(QIODevice::ReadOnly), "read committed session");
    const auto audit =
        QJsonDocument::fromJson(session.readAll()).object()["recognitionAudit"].toArray();
    session.close();
    require(audit.size() == 1 && audit[0].toObject()["capture"].toObject() == firstCapture,
            "committed race links to the exact frozen frame");
    auto *token = dock->findChild<QLineEdit *>("discordToken");
    token->setText("offline-bot-token");
    dock->findChild<QLineEdit *>("discordChannel")->setText("987654321012345678");
    dock->findChild<QPushButton *>("discordSave")->click();
    require(token->text().isEmpty() && discordApi.count == 0, "saving bot settings does not send");
    auto *send = dock->findChild<QPushButton *>("discordSend");
    require(send->isEnabled(), "send enabled after score");
    send->click();
    send->click();
    pump(image, 500);
    require(discordApi.count == 1, "manual Discord double click sends once");
    const auto sent =
        QJsonDocument::fromJson(discordApi.request.mid(discordApi.request.indexOf("\r\n\r\n") + 4))
            .object();
    require(sent["content"].toString() == expectedLine &&
                sent["allowed_mentions"].toObject()["parse"].toArray().isEmpty(),
            "Discord preserves score format and disables mentions");
    pump(blank, 4000);
    wait.restart();
    while (!table->isVisible() && wait.elapsed() < 30000)
      pump(image, 50);
    const auto warmMs = wait.elapsed();
    require(table->isVisible(), "CPU template recognition on the next race");
    checkRows();
    require(captureFiles().size() == 2, "next race produces a new image pair");
    dock->findChild<QPushButton *>("cancel")->click();
    require(scores->text() == expectedLine, "skip preserves score");
    require(captureFiles().size() == 2, "skipped results keep their images");
    require(dock->grab().save(args[6]), "Jev dock preview");
    pump(blank, 4000);
    wait.restart();
    while (captureFiles().size() < 3 && wait.elapsed() < 30000)
      pump(image, 50);
    require(captureFiles().size() == 3 && table->isVisible(), "result saved before stopping");
    assist->click();
    pump(image, 100);
    start->click();
    pump(image, 2800);
    require(scores->text() == expectedLine && !table->isVisible(),
            "stop discards any late local answer");
    require(captureFiles().size() == 3, "stopping inference preserves the result frame");

    auto *automatic = dock->findChild<QCheckBox *>("autoCommit");
    require(automatic && automatic->isEnabled(), "automatic setting available while stopped");
    automatic->setChecked(true);
    if (!manualLabels)
      require(QFile::rename(config.path() + "/local-runtime.json",
                            config.path() + "/local-runtime.json.disabled"),
              "remove model configuration to prove CPU mode independence");
    start->click();
    wait.restart();
    while (state->text().contains(QStringLiteral("読み込み")) && wait.elapsed() < 120000)
      pump(blank, 100);
    require(start->text() == QStringLiteral("集計停止"), "automatic mode works without a model");
    pump(blank, 3500);
    const auto automaticLine = manualLabels
                                   ? QStringLiteral("【d】50／T 28 (+22)／A 28 (+22)／F 24 "
                                                    "(+26)／た 20 (+30)／C 14 (+36)／現在1位／＠10")
                                   : QStringLiteral("【M】34／う 34 (±0)／L 32 (+2)／A 28 (+6)／B "
                                                    "18 (+16)／R 18 (+16)／現在1位／＠10");
    wait.restart();
    while (scores->text() != automaticLine && wait.elapsed() < 30000)
      pump(image, 50);
    const auto automaticMs = wait.elapsed();
    if (scores->text() != automaticLine || table->isVisible()) {
      std::cerr << "Automatic status=" << state->text().toStdString()
                << " score=" << scores->text().toStdString() << '\n';
      if (table->isVisible())
        for (int row = 0; row < 12; ++row) {
          const auto *box = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
          std::cerr << "Automatic rank " << row + 1 << "="
                    << (box ? box->currentData().toString().toStdString() : "missing") << '\n';
        }
    }
    require(scores->text() == automaticLine && !table->isVisible(),
            "valid twelve rows automatically commit exactly one race");
    pump(image, 1000);
    require(scores->text() == automaticLine, "automatic mode duplicate guard");
    require(captureFiles().size() == 4, "automatic scoring saves exactly one image pair");

    QImage occluded = image.copy();
    for (int y = 85; y < 139; ++y)
      for (int x = 1013; x < 1143; ++x)
        occluded.setPixel(x, y, qRgb(0, 0, 0));
    pump(blank, 4000);
    wait.restart();
    while (!table->isVisible() && wait.elapsed() < 30000)
      pump(occluded, 50);
    require(table->isVisible(), "unknown initial opens manual review in automatic mode");
    const auto *unknown = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
    require(unknown && unknown->currentData().toString().isEmpty() &&
                scores->text() == automaticLine,
            "automatic mode never fills unknown rows or changes the score");
    require(captureFiles().size() == 5, "unreadable result is retained for accuracy review");
    dock->findChild<QPushButton *>("cancel")->click();
    start->click();

    const auto evidenceDirectory = args[6] + ".captures";
    require(QDir().mkpath(evidenceDirectory), "capture evidence folder");
    for (const auto &file : captures.entryList(QDir::Files))
      require(QFile::copy(captures.filePath(file), QDir(evidenceDirectory).filePath(file)),
              "preserve actual OBS capture evidence outside temporary profile");
    const auto report = QJsonObject{{"first_result_ms", firstMs},
                                    {"first_capture_ms", firstCaptureMs},
                                    {"automatic_result_captures", 5},
                                    {"captured_before_recognition", true},
                                    {"capture_pixels_preserved", true},
                                    {"capture_directory", evidenceDirectory},
                                    {"jev_optional_assistance_ms", assistanceMs},
                                    {"cpu_without_model", true},
                                    {"warm_result_ms", warmMs},
                                    {"valid_rows", 12},
                                    {"scoreLine", expectedLine},
                                    {"real_gpu", !manualLabels},
                                    {"discord_mock_messages", discordApi.count},
                                    {"automatic_result_ms", automaticMs},
                                    {"automatic_valid_committed", true},
                                    {"automatic_unknown_held", true}};
    QFile result(args[6] + ".json");
    require(result.open(QIODevice::WriteOnly), "timing report");
    result.write(QJsonDocument(report).toJson());
    QString error;
    require(quickscore::saveStoredCredential(config.path(), "DiscordBot", {}, error),
            "remove isolated credential");
    dock.reset();
    obs_set_output_source(0, nullptr);
    obs_source_release(source);
    obs_shutdown();
    std::cout << "PASS OBS capture, optional real Jev calibration, CPU 12/12 2v2, format, "
                 "duplicate guard, stop, images, manual Discord, automatic commit, unknown held\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
