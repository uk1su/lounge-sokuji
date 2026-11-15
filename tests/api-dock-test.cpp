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
    MockServer discordApi, jevApi;
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
    jevApi.body = QJsonDocument(QJsonObject{{"model", "jev-1.14.0"},
                                            {"answers", answers},
                                            {"usage", QJsonObject{{"input_tokens", 1200}}}})
                      .toJson();
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
    pump(blank, 3500);
    QElapsedTimer elapsed;
    elapsed.start();
    while (!table->isVisible() && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(table->isVisible() && (live || jevApi.count == 1), "OBS to one API request to review");
    require(scores->text().contains(QStringLiteral("＠12")), "manual default");
    for (int row = 0; row < 12; ++row)
      require(qobject_cast<QComboBox *>(table->cellWidget(row, 2))->currentData().toString() ==
                  expected[row],
              "twelve API rows");
    dock->findChild<QPushButton *>("confirm")->click();
    const auto firstLine = QStringLiteral(
        "【M】17／う 17 (±0)／L 16 (+1)／A 14 (+3)／B 9 (+8)／R 9 (+8)／現在1位／＠11");
    require(scores->text() == firstLine, "score format");
    if (live) {
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
    pump(image, 1000);
    require(jevApi.count == 1, "duplicate guard");
    pump(blank, 3500);
    jevApi.delay = 800;
    elapsed.restart();
    while (jevApi.count < 2 && elapsed.elapsed() < 10000)
      pump(image, 50);
    start->click();
    pump(image, 1200);
    require(scores->text() == firstLine && !table->isVisible(), "stop cancels late API result");
    jevApi.delay = 0;
    dock->findChild<QCheckBox *>("autoCommit")->setChecked(true);
    start->click();
    pump(blank, 3500);
    elapsed.restart();
    const auto secondLine = QStringLiteral(
        "【M】34／う 34 (±0)／L 32 (+2)／A 28 (+6)／B 18 (+16)／R 18 (+16)／現在1位／＠10");
    while (scores->text() != secondLine && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(scores->text() == secondLine && jevApi.count == 3, "validated API auto commit");
    const auto automaticMs = elapsed.elapsed();
    pump(blank, 3500);
    jevApi.status = 429;
    elapsed.restart();
    while (!table->isVisible() && elapsed.elapsed() < 10000)
      pump(image, 50);
    require(table->isVisible() && scores->text() == secondLine && jevApi.count == 4, "429 held");
    pump(image, 1000);
    require(jevApi.count == 4, "no automatic retries");
    require(dock->grab().save(args[6]), "API UI preview");
    require(QDir(config.path() + "/captures").entryList({"*.json"}, QDir::Files).size() == 4,
            "exact images saved");
    QFile report(args[6] + ".json");
    require(report.open(QIODevice::WriteOnly), "report");
    report.write(QJsonDocument(QJsonObject{{"real_obs_capture", true},
                                           {"mock_api_requests", jevApi.count},
                                           {"automatic_ms", automaticMs},
                                           {"scoreLine", firstLine},
                                           {"stop_late_reply_discarded", true},
                                           {"rate_limit_held", true}})
                     .toJson());
    start->click();
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
