#include "main-dock.hpp"
#include "fast-reader.hpp"
#include "result-capture.hpp"
#include "scoreboard-source.hpp"
#include "session-store.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <obs-module.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace quickscore {
namespace {
int playersForFormat(int index) {
  const int values[]{2, 3, 4, 6};
  return values[std::clamp(index, 0, 3)];
}
QWidget *buttons(std::initializer_list<QWidget *> widgets) {
  auto *row = new QWidget;
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  for (auto *w : widgets)
    layout->addWidget(w);
  return row;
}
QJsonArray evidenceJson(const Analysis &a) {
  QJsonArray result;
  for (size_t i = 0; i < a.standings.size(); ++i) {
    const auto &s = a.standings[i];
    QJsonObject row{
        {"rank", s.rank}, {"tag", QString::fromStdString(s.tag)}, {"confidence", s.confidence}};
    if (i < a.evidence.size()) {
      row.insert("ocr", a.evidence[i].text);
      row.insert("method", a.evidence[i].method);
      row.insert("similarity", a.evidence[i].similarity);
    }
    result.append(row);
  }
  return result;
}
bool namesAgree(const QImage &previous, const QImage &current) {
  if (previous.isNull() || current.isNull())
    return false;
  for (int row = 0; row < 12; ++row) {
    const auto a = Analyzer::rowImage(previous, row)
                       .copy(8, 2, 92, 50)
                       .convertToFormat(QImage::Format_Grayscale8);
    const auto b = Analyzer::rowImage(current, row)
                       .copy(8, 2, 92, 50)
                       .convertToFormat(QImage::Format_Grayscale8);
    int difference = 0;
    for (int y = 0; y < a.height(); y += 2)
      for (int x = 0; x < a.width(); x += 2)
        difference += std::abs(int(a.constScanLine(y)[x]) - int(b.constScanLine(y)[x]));
    if (difference > (a.width() / 2) * (a.height() / 2) * 8)
      return false;
  }
  return true;
}
} // namespace
MainDock::MainDock(QString directory, QWidget *parent, QUrl testDiscordEndpoint,
                   QUrl testApiEndpoint)
    : QWidget(parent), configDirectory_(std::move(directory)) {
  clock_.start();
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  outer->addWidget(scroll);
  auto *body = new QWidget;
  scroll->setWidget(body);
  auto *layout = new QVBoxLayout(body);
  auto *title = new QLabel(tr("QUICKSCORE  0.8.1 / Jev API"));
  title->setStyleSheet("font-size:20px;font-weight:700;");
  layout->addWidget(title);
  auto *setup = new QGroupBox(tr("ゲーム映像とチーム"));
  auto *form = new QFormLayout(setup);
  source_ = new QComboBox;
  refresh_ = new QPushButton(tr("再読込"));
  form->addRow(tr("元映像"), buttons({source_, refresh_}));
  format_ = new QComboBox;
  format_->addItems({"2v2", "3v3", "4v4", "6v6"});
  format_->setCurrentIndex(1);
  tags_ = new QLineEdit("A, B, C, D");
  tags_->setPlaceholderText(tr("一番左に自分のチームタグを入れてください (例: A, B, C, D)"));
  form->addRow(tr("形式"), format_);
  form->addRow(tr("タグ（一番左に自分のチームタグを入れてください）"), tags_);
  auto *hint =
      new QLabel(tr("キャプチャーボードなど、16:9の元ゲーム映像を選んでください。\n"
                    "※タグの一番左が自分のチームとして認識され、点差や順位が計算されます。\n"
                    "名前の先頭1文字でチームを判定します。チーム間で先頭文字は重複できません。"));
  hint->setWordWrap(true);
  form->addRow(hint);
  layout->addWidget(setup);
  auto *fastGroup = new QGroupBox(tr("交流戦版と同じ画像照合"));
  auto *fastForm = new QFormLayout(fastGroup);
  fastStatus_ = new QLabel;
  fastStatus_->setObjectName("fastStatus");
  fastStatus_->setWordWrap(true);
  fastForm->addRow(fastStatus_);
  clearFast_ = new QPushButton(tr("名前の見本を登録し直す"));
  clearFast_->setObjectName("clearFast");
  fastForm->addRow(clearFast_);
  connect(clearFast_, &QPushButton::clicked, this, [this] {
    if (running_)
      return;
    fastSamples_.clear();
    fastSource_.clear();
    QString error;
    saveSamples(configDirectory_ + "/fast-templates.json", fastSamples_, error);
    saveSettings();
    updateFastStatus();
    if (!error.isEmpty())
      fastStatus_->setText(tr("見本の更新を保存できません: %1").arg(error));
  });
  autoCommit_ = new QCheckBox(tr("検証を通った結果を自動集計する"));
  autoCommit_->setObjectName("autoCommit");
  fastForm->addRow(autoCommit_);
  layout->addWidget(fastGroup);
  auto *reading = new QGroupBox(tr("Jev API / TypeSafe"));
  auto *readingForm = new QFormLayout(reading);
  api_ = std::make_unique<JevApiReader>(configDirectory_, testApiEndpoint);
  apiEnabled_ = new QCheckBox(tr("Jev APIでリザルトを判定する"));
  apiEnabled_->setObjectName("jevApiEnabled");
  apiEnabled_->setChecked(true);
  readingForm->addRow(apiEnabled_);
  auto *apiHint = new QLabel(tr("12人の名前の先頭部分だけをTypeSafeへ送信します。モデルのダウンロー"
                                "ドは不要です。初期設定では判定後に12人を確認します。"));
  apiHint->setWordWrap(true);
  readingForm->addRow(apiHint);
  apiKey_ = new QLineEdit;
  apiKey_->setObjectName("jevApiKey");
  apiKey_->setEchoMode(QLineEdit::Password);
  apiKey_->setPlaceholderText(tr("保存後は空欄になります"));
  readingForm->addRow(tr("APIキー"), apiKey_);
  setupLocal_ = new QPushButton(tr("APIキーを保存"));
  setupLocal_->setObjectName("saveJevApiKey");
  readingForm->addRow(setupLocal_);
  localStatus_ =
      new QLabel(api_->configurationError().isEmpty() ? tr("APIキー設定済み。モデル: jev-1.14.0")
                                                      : api_->configurationError());
  localStatus_->setWordWrap(true);
  readingForm->addRow(localStatus_);
  connect(setupLocal_, &QPushButton::clicked, this, [this] {
    const auto key = apiKey_->text().trimmed();
    QString error;
    if (key.isEmpty() || key.contains('\n') || key.contains('\r')) {
      localStatus_->setText(tr("APIキーを入力してください。"));
      return;
    }
    const bool ok = saveStoredCredential(configDirectory_, "JevApi", key, error);
    apiKey_->clear();
    localStatus_->setText(ok ? tr("APIキーを保存しました。保存だけでは送信しません。") : error);
  });
  layout->addWidget(reading);
  start_ = new QPushButton(tr("集計開始"));
  manual_ = new QPushButton(tr("現在のリザルトを確認"));
  manual_->setEnabled(false);
  layout->addWidget(buttons({start_, manual_}));
  status_ = new QLabel;
  status_->setWordWrap(true);
  layout->addWidget(status_);
  diagnostic_ = new QLabel(tr("映像待ち"));
  diagnostic_->setWordWrap(true);
  layout->addWidget(diagnostic_);
  preview_ = new QLabel(tr("集計開始後、ここに解析対象の映像を表示します"));
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setMinimumHeight(130);
  preview_->setStyleSheet("background:#15191f;border:1px solid #424850;");
  layout->addWidget(preview_);
  captureStatus_ = new QLabel(tr("リザルト判定時に、画面全体と順位一覧の画像を自動保存します。"));
  captureStatus_->setObjectName("captureStatus");
  captureStatus_->setWordWrap(true);
  layout->addWidget(captureStatus_);
  auto *captureFolder = new QPushButton(tr("リザルト画像の保存フォルダーを開く"));
  captureFolder->setObjectName("captureFolder");
  layout->addWidget(captureFolder);
  connect(captureFolder, &QPushButton::clicked, this, [this] {
    const auto directory = QDir(configDirectory_).absoluteFilePath("captures");
    if (!QDir().mkpath(directory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
      captureStatus_->setText(
          tr("保存フォルダーを開けません: %1").arg(QDir::toNativeSeparators(directory)));
  });
  scores_ = new QLabel;
  scores_->setWordWrap(true);
  scores_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  scores_->setStyleSheet(
      "font-size:17px;font-weight:700;color:#eceff1;background:#10141a;border:1px solid "
      "#37474f;border-radius:6px;padding:8px 12px;");
  layout->addWidget(scores_);
  auto *discordGroup = new QGroupBox(tr("Discordへの手動送信"));
  auto *discordForm = new QFormLayout(discordGroup);
  discordToken_ = new QLineEdit;
  discordToken_->setEchoMode(QLineEdit::Password);
  discordToken_->setPlaceholderText(tr("Discord Botトークン（保存後は空欄になります）"));
  discordToken_->setObjectName("discordToken");
  discordChannel_ = new QLineEdit;
  discordChannel_->setPlaceholderText(tr("送信先チャンネルID または チャンネルURL"));
  discordChannel_->setObjectName("discordChannel");
  discordSave_ = new QPushButton(tr("Discord設定を保存"));
  discordSave_->setObjectName("discordSave");
  discordDelete_ = new QPushButton(tr("Botトークンを削除"));
  discordSend_ = new QPushButton(tr("Discordへ送信"));
  discordSend_->setObjectName("discordSend");
  discordStatus_ = new QLabel;
  discordStatus_->setWordWrap(true);
  discordStatus_->setObjectName("discordStatus");
  discordForm->addRow(tr("Botトークン"), discordToken_);
  discordForm->addRow(tr("送信先"), discordChannel_);
  discordForm->addRow(buttons({discordSave_, discordDelete_}));
  discordForm->addRow(discordSend_);
  discordForm->addRow(discordStatus_);
  auto *discordHint = new QLabel(
      tr("Botをサーバーに参加させ、送信先の閲覧・メッセージ送信権限を付与してください。\n"
         "上の確定済み集計を1行で送信します。ボタンを押したときだけ送信します。"));
  discordHint->setWordWrap(true);
  discordForm->addRow(discordHint);
  layout->addWidget(discordGroup);
  discord_ = std::make_unique<DiscordSender>(nullptr, testDiscordEndpoint);
  connect(discordSave_, &QPushButton::clicked, this, [this] {
    const auto channel = discordChannelId(discordChannel_->text());
    if (channel.isEmpty()) {
      discordStatus_->setText(tr("チャンネルID または DiscordのチャンネルURLを入力してください。"));
      return;
    }
    QString error;
    if (!discordToken_->text().trimmed().isEmpty() &&
        !saveStoredCredential(configDirectory_, "DiscordBot", discordToken_->text(), error)) {
      discordToken_->clear();
      discordStatus_->setText(error);
      return;
    }
    discordToken_->clear();
    discordChannel_->setText(channel);
    saveSettings();
    discordStatus_->setText(
        loadStoredCredential(configDirectory_, "DiscordBot").isEmpty()
            ? tr("Botトークン未設定です。入力して「Discord設定を保存」を押してください。")
            : tr("Discord設定を保存しました。確定済み集計を「Discordへ送信」で送れます。"));
    updateDiscord();
  });
  connect(discordDelete_, &QPushButton::clicked, this, [this] {
    QString error;
    const bool ok = saveStoredCredential(configDirectory_, "DiscordBot", {}, error);
    discordToken_->clear();
    discordStatus_->setText(ok ? tr("Botトークンを削除しました。") : error);
    updateDiscord();
  });
  connect(discordSend_, &QPushButton::clicked, this, [this] { sendDiscord(); });
  connect(discordChannel_, &QLineEdit::textChanged, this, [this] { updateDiscord(); });
  undo_ = new QPushButton(tr("Undo"));
  auto *reset = new QPushButton(tr("集計リセット"));
  auto *diagnose = new QPushButton(tr("診断を保存"));
  layout->addWidget(buttons({undo_, reset, diagnose}));
  reviewHelp_ = new QLabel(tr("12人のチームを確認してください。判定が曖昧な行は未選択です。"));
  reviewHelp_->setWordWrap(true);
  reviewHelp_->hide();
  layout->addWidget(reviewHelp_);
  correction_ = new QTableWidget(12, 3);
  correction_->setHorizontalHeaderLabels({tr("順位"), tr("名前画像"), tr("チーム")});
  correction_->verticalHeader()->hide();
  correction_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  correction_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  correction_->setMinimumHeight(480);
  correction_->hide();
  layout->addWidget(correction_);
  confirm_ = new QPushButton(tr("確認した内容で集計"));
  cancel_ = new QPushButton(tr("この結果をスキップ"));
  jevAssist_ = new QPushButton(tr("Jev APIでチームを再判定する"));
  jevAssist_->setObjectName("jevAssist");
  for (auto *b : {confirm_, cancel_, jevAssist_})
    b->hide();
  layout->addWidget(jevAssist_);
  layout->addWidget(buttons({confirm_, cancel_}));
  layout->addStretch();
  connect(refresh_, &QPushButton::clicked, this, [this] { refreshSources(); });
  connect(format_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this] { applyFormat(); });
  connect(start_, &QPushButton::clicked, this, [this] { startStop(); });
  connect(manual_, &QPushButton::clicked, this, [this] {
    if (latest_.complete() && !gate_.locked()) {
      requestRecognition(latest_);
    }
  });
  connect(confirm_, &QPushButton::clicked, this, [this] { confirmPending(); });
  connect(jevAssist_, &QPushButton::clicked, this, [this] { requestJevAssistance(); });
  connect(cancel_, &QPushButton::clicked, this, [this] {
    gate_.lock();
    cancelReview();
    setStatus(tr("スキップしました。次のレースを待機します。"));
  });
  connect(undo_, &QPushButton::clicked, this, [this] { undo(); });
  connect(reset, &QPushButton::clicked, this, [this] { resetSession(); });
  connect(diagnose, &QPushButton::clicked, this, [this] { exportDiagnostic(); });
  preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  source_->setObjectName("source");
  format_->setObjectName("format");
  tags_->setObjectName("tags");
  start_->setObjectName("start");
  scores_->setObjectName("scores");
  status_->setObjectName("status");
  diagnostic_->setObjectName("diagnostic");
  manual_->setObjectName("manual");
  correction_->setObjectName("correction");
  confirm_->setObjectName("confirm");
  cancel_->setObjectName("cancel");
  undo_->setObjectName("undo");
  loadSettings();
  refreshSources();
  connect(source_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this] { updateFastStatus(); });
  connect(tags_, &QLineEdit::textChanged, this, [this] { updateFastStatus(); });
  QString sampleError;
  if (!loadSamples(configDirectory_ + "/fast-templates.json", fastSamples_, sampleError)) {
    fastSamples_.clear();
    blog(LOG_WARNING, "[MK8DX QuickScore] fast templates could not be loaded: %s",
         sampleError.toUtf8().constData());
  }
  updateFastStatus();
  QString error;
  if (!loadSession(configDirectory_ + "/session.json", session_, audit_, error)) {
    storageOk_ = false;
    setStatus(error, "#ff8a80");
  } else {
    if (!session_.races().empty()) {
      const int p = session_.playersPerTeam();
      format_->setCurrentIndex(p == 2 ? 0 : p == 3 ? 1 : p == 4 ? 2 : 3);
      QStringList tags;
      for (const auto &s : session_.tags())
        tags << QString::fromStdString(s);
      tags_->setText(tags.join(", "));
    }
    setStatus(tr("準備完了。開始後、リザルト以外の画面を3秒映すと自動集計を待機します。"));
  }
  char *path = obs_module_file("templates.mkr");
  try {
    if (!path)
      throw std::runtime_error("templates.mkr missing");
    analyzer_ = std::make_unique<Analyzer>(QString::fromUtf8(path));
  } catch (const std::exception &e) {
    setStatus(tr("認識テンプレートを読み込めません: %1").arg(QString::fromUtf8(e.what())),
              "#ff8a80");
    start_->setEnabled(false);
  }
  bfree(path);
  capture_ = std::make_unique<FrameCapture>([this](Frame frame) {
    if (frameQueued_.exchange(true))
      return;
    QMetaObject::invokeMethod(
        this,
        [this, frame = std::move(frame)]() mutable {
          frameQueued_ = false;
          receiveFrame(std::move(frame));
        },
        Qt::QueuedConnection);
  });
  worker_ = std::thread([this] { workerLoop(); });
  auto *timer = new QTimer(this);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, [this] {
    if (!running_ || reviewing_)
      return;
    if (clock_.elapsed() - lastFrameMs_ > 2500) {
      if (api_->busy()) {

        api_->cancel();
        ++generation_;
      }
      setStatus(tr("映像が届いていません。選択ソースの表示・接続を確認してください。"), "#ffb74d");
    } else if (clock_.elapsed() - lastAnalysisMs_ > 5000)
      setStatus(tr("認識処理中です。処理時間が長くなっています。"), "#ffb74d");
  });
  timer->start();
  updateScores();
}
MainDock::~MainDock() {
  running_ = false;
  ++generation_;

  api_->cancel();
  if (localSetup_.state() != QProcess::NotRunning) {
    QProcess::execute("taskkill.exe",
                      {"/PID", QString::number(localSetup_.processId()), "/T", "/F"});
    localSetup_.waitForFinished(300);
  }
  discord_->cancel();
  capture_->stop();
  capture_.reset();
  {
    std::lock_guard<std::mutex> lock(workerMutex_);
    workerStop_ = true;
  }
  workerCv_.notify_one();
  if (worker_.joinable())
    worker_.join();
  saveSettings();
}
void MainDock::refreshSources() {
  const QString old = source_->currentData().toString();
  source_->clear();
  obs_enum_sources(
      [](void *data, obs_source_t *s) {
        if ((obs_source_get_output_flags(s) & OBS_SOURCE_VIDEO) &&
            obs_source_get_type(s) == OBS_SOURCE_TYPE_INPUT &&
            QString::fromUtf8(obs_source_get_id(s)) != "mk8dx_quickscore_scoreboard") {
          const auto name = QString::fromUtf8(obs_source_get_name(s));
          static_cast<QComboBox *>(data)->addItem(name, name);
        }
        return true;
      },
      source_);
  source_->addItem(tr("OBSキャンバス全体（ゲームが全画面の場合）"), "__canvas__");
  const int found = source_->findData(old.isEmpty() ? savedSource_ : old);
  if (found >= 0)
    source_->setCurrentIndex(found);
}
void MainDock::applyFormat() {
  QStringList values;
  for (int i = 0; i < 12 / playersForFormat(format_->currentIndex()); ++i)
    values << QString(QChar('A' + i));
  tags_->setText(values.join(", "));
}
std::vector<std::string> MainDock::configuredTags() const {
  std::vector<std::string> out;
  for (const auto &p : tags_->text().split(',')) {
    auto v = p.trimmed().toStdString();
    if (!v.empty() && std::find(out.begin(), out.end(), v) == out.end())
      out.push_back(v);
  }
  return out;
}
void MainDock::startStop() {
  if (running_) {
    running_ = false;
    ++generation_;

    api_->cancel();
    capture_->stop();
    gate_.pause();
    cancelReview();
    manual_->setEnabled(false);
    start_->setText(tr("集計開始"));
    for (auto *w : std::initializer_list<QWidget *>{source_, format_, tags_, refresh_, autoCommit_,
                                                    setupLocal_, clearFast_, apiEnabled_, apiKey_})
      w->setEnabled(true);
    setStatus(tr("停止中"));
    return;
  }
  if (!storageOk_) {
    QMessageBox::warning(this, tr("保存データの確認"),
                         tr("保存データに読み込みエラーがあります。元ファイルを退避・確認してOBSを"
                            "再起動してください。"));
    return;
  }
  if (apiEnabled_->isChecked() && !api_->configurationError().isEmpty()) {
    setStatus(api_->configurationError(), "#ffb74d");
    return;
  }
  auto tags = configuredTags();
  const int players = playersForFormat(format_->currentIndex());
  if (int(tags.size()) * players != 12) {
    QMessageBox::warning(this, tr("設定エラー"),
                         tr("チーム数 × 人数が12になるように設定してください。"));
    return;
  }
  QString tagError;
  if (!validateTeamTags(tags, &tagError)) {
    QMessageBox::warning(this, tr("設定エラー"), tagError);
    return;
  }
  if (localSetup_.state() != QProcess::NotRunning) {
    setStatus(tr("モデルの準備が完了するまでお待ちください。"));
    return;
  }
  if (!session_.races().empty() &&
      (session_.tags() != tags || session_.playersPerTeam() != players)) {
    QMessageBox::warning(this, tr("設定エラー"),
                         tr("チームを変更する前に集計リセットしてください。"));
    return;
  }
  obs_source_t *selected = nullptr;
  const bool canvas = source_->currentData().toString() == "__canvas__";
  if (!canvas) {
    selected = obs_get_source_by_name(source_->currentData().toString().toUtf8().constData());
    if (!selected) {
      setStatus(tr("映像ソースが見つかりません。再読込してください。"), "#ff8a80");
      return;
    }
  }
  session_.configure(tags, players);
  ++generation_;
  gate_.pause();
  requestIssued_ = false;
  requestCycle_ = gate_.cycle();
  stableNames_ = {};
  latest_ = {};
  latestImage_ = {};
  latestFrameReceivedAt_ = {};
  resultCapture_ = {};
  capture_->setSource(selected);
  if (selected)
    obs_source_release(selected);
  running_ = true;
  lastFrameMs_ = lastAnalysisMs_ = clock_.elapsed();
  capture_->start();
  for (auto *w : std::initializer_list<QWidget *>{source_, format_, tags_, refresh_, autoCommit_,
                                                  setupLocal_, clearFast_, apiEnabled_, apiKey_})
    w->setEnabled(false);
  start_->setText(tr("集計停止"));
  setStatus(tr("映像を確認中。リザルト以外を3秒映すと自動待機します。現在の結果は確認ボタンから手動"
               "集計できます。"));
  saveSettings();
  updateScores();
  updateFastStatus();
}
void MainDock::receiveFrame(Frame frame) {
  if (!running_)
    return;
  lastFrameMs_ = clock_.elapsed();
  QImage view(frame.rgba.data(), frame.width, frame.height, frame.stride, QImage::Format_RGBA8888);
  latestImage_ = view.copy();
  if (!reviewing_)
    preview_->setPixmap(QPixmap::fromImage(latestImage_)
                            .scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  // Keep receiving the current image for the final stability check, but avoid
  // repeating the expensive layout search while the GPU is making a decision.
  if (api_->busy())
    return;
  if (busy_ || reviewing_ || !analyzer_)
    return;
  {
    std::lock_guard<std::mutex> lock(workerMutex_);
    jobImage_ = latestImage_;
    jobFrameReceivedAt_ = QDateTime::currentDateTimeUtc();
    jobTags_ = session_.tags();
    jobSamples_ =
        fastSource_ == source_->currentData().toString() ? fastSamples_ : std::vector<TagSample>{};
    jobGeneration_ = generation_;
    jobReady_ = true;
    busy_ = true;
  }
  workerCv_.notify_one();
}
void MainDock::workerLoop() {
  for (;;) {
    QImage image;
    std::vector<std::string> tags;
    std::vector<TagSample> samples;
    uint64_t generation;
    QDateTime frameReceivedAt;
    {
      std::unique_lock<std::mutex> lock(workerMutex_);
      workerCv_.wait(lock, [this] { return workerStop_ || jobReady_; });
      if (workerStop_)
        return;
      image = std::move(jobImage_);
      tags = jobTags_;
      samples = jobSamples_;
      generation = jobGeneration_;
      frameReceivedAt = jobFrameReceivedAt_;
      jobReady_ = false;
    }
    Analysis result;
    QElapsedTimer elapsed;
    elapsed.start();
    try {
      result = analyzer_->read(image, tags, {}, false, true);
      result = readFast(std::move(result), tags, samples);
    } catch (const std::exception &e) {
      result.error = QString::fromUtf8(e.what());
    }
    const qint64 duration = elapsed.elapsed();
    QMetaObject::invokeMethod(
        this,
        [this, result = std::move(result), generation, duration, frameReceivedAt]() mutable {
          receiveAnalysis(std::move(result), generation, duration, frameReceivedAt);
        },
        Qt::QueuedConnection);
  }
}
void MainDock::receiveAnalysis(Analysis result, uint64_t generation, qint64 duration,
                               QDateTime frameReceivedAt) {
  busy_ = false;
  if (!running_ || generation != generation_)
    return;
  lastAnalysisMs_ = clock_.elapsed();
  latest_ = std::move(result);
  latestFrameReceivedAt_ = frameReceivedAt;
  if (!latest_.error.isEmpty()) {
    const auto error = latest_.error;
    startStop();
    setStatus(error, "#ff8a80");
    return;
  }
  const int recognized = int(std::count_if(latest_.standings.begin(), latest_.standings.end(),
                                           [](const auto &s) { return !s.tag.empty(); }));
  diagnostic_->setText(
      tr("映像 %1×%2 → 1920×1080 ｜ 順位 %3/12 ｜ 加点記号 %4/12 ｜ タグ %5/12 ｜ %6 ms")
          .arg(latestImage_.width())
          .arg(latestImage_.height())
          .arg(latest_.layout.anchors)
          .arg(latest_.layout.pluses)
          .arg(recognized)
          .arg(duration));
  {
    const bool agreed = stableNames_.isNull() || namesAgree(stableNames_, latest_.image);
    stableNames_ = latest_.complete() ? latest_.image : QImage{};
    const bool stable = gate_.observe(latest_.layout.visible, latest_.complete(),
                                      agreed ? "decision" : "", uint64_t(clock_.elapsed()));
    if (requestCycle_ != gate_.cycle()) {

      // Warm-up spans the initial non-result screen; only cancel a live read.
      if (api_->ready() && api_->busy())
        api_->cancel();
      requestIssued_ = false;
      requestCycle_ = gate_.cycle();
    }
    const bool readerBusy = api_->busy();
    manual_->setEnabled(latest_.complete() && !gate_.locked() && !readerBusy);
    if (readerBusy) {
      setStatus(tr("Jev APIの応答を待っています。停止で中断できます。"));
    } else if (stable && !requestIssued_) {
      requestRecognition(latest_);
    } else if (gate_.locked()) {
      setStatus(tr("このリザルトは処理済みです。次のレースを待機中。"));
    } else if (!gate_.armed()) {
      setStatus(tr("開始準備：リザルト以外を3秒映すか「現在のリザルトを確認」を押してください。"));
    } else if (!latest_.layout.visible) {
      setStatus(tr("リザルト待機中。順位の枠を画像照合しています。"));
    } else {
      setStatus(tr("リザルトと先頭文字の画像が安定するまで待機中（12順位・加点記号が必要）。"));
    }
    return;
  }
}
void MainDock::requestJevAssistance() {
  if (!running_ || !reviewing_ || pending_.image.isNull() || api_->busy())
    return;
  const auto configurationError = api_->configurationError();
  if (!configurationError.isEmpty()) {
    setStatus(tr("Jev APIキーが未設定です。集計停止後にAPIキーを保存してください。"), "#ffb74d");
    return;
  }
  const auto generation = generation_;
  const auto captured = pending_;
  jevAssist_->setEnabled(false);
  confirm_->setEnabled(false);
  correction_->setEnabled(false);
  auto complete = [this, generation](Analysis result) {
    if (!running_ || !reviewing_ || generation != generation_)
      return;
    // Preserve choices the user had already made before asking for assistance.
    for (int row = 0; row < 12 && size_t(row) < result.standings.size(); ++row) {
      const auto *box = qobject_cast<QComboBox *>(correction_->cellWidget(row, 2));
      const auto selected = box->currentData().toString();
      if (!selected.isEmpty()) {
        result.standings[size_t(row)].tag = selected.toStdString();
        result.standings[size_t(row)].confidence = 1;
      }
    }
    api_->cancel();
    confirm_->setEnabled(true);
    correction_->setEnabled(true);
    jevAssist_->setEnabled(true);
    review(result);
    setStatus(
        result.error.isEmpty()
            ? tr("Jev APIの判定が完了しました。12人のチームを確認して見本を登録してください。")
            : result.error + tr(" 手動でチームを選択できます。"),
        "#ffb74d");
  };
  auto read = [this, generation, captured, complete](QString error) mutable {
    if (!running_ || !reviewing_ || generation != generation_)
      return;
    if (!error.isEmpty()) {
      auto failed = captured;
      failed.error = error;
      complete(std::move(failed));
      return;
    }
    setStatus(tr("保存した名前画像をJev APIで判定しています。"));
    api_->read(captured, session_.tags(), std::move(complete));
  };
  if (api_->ready())
    read({});
  else {
    setStatus(tr("Jev APIへ接続しています。"));
    api_->prepare(std::move(read));
  }
}
void MainDock::requestRecognition(const Analysis &captured) {
  if (!running_ || reviewing_ || api_->busy() || requestIssued_ || !captured.complete())
    return;
  requestIssued_ = true;
  requestCycle_ = gate_.cycle();
  gate_.lock(); // Reserve before saving and inference; failures never cause an automatic retry.
  manual_->setEnabled(false);
  const auto generation = generation_;
  const auto cycle = requestCycle_;
  const auto requestedAt = clock_.elapsed();
  QJsonArray tags;
  for (const auto &tag : session_.tags())
    tags.append(QString::fromStdString(tag));
  const auto capture = saveResultCapture(
      QDir(configDirectory_).absoluteFilePath("captures"), captured.image,
      QJsonObject{{"version", "0.8.1"},
                  {"source", source_->currentText()},
                  {"frameReceivedAt", latestFrameReceivedAt_.toString(Qt::ISODateWithMs)},
                  {"format", format_->currentText()},
                  {"tags", tags},
                  {"anchors", captured.layout.anchors},
                  {"pluses", captured.layout.pluses}});
  resultCapture_ = capture.files;
  if (capture.error.isEmpty()) {
    captureStatus_->setText(
        tr("判定に使うリザルト画像を保存しました。未確定・スキップ時も残ります。"));
    captureStatus_->setStyleSheet({});
    captureStatus_->setToolTip(QDir::toNativeSeparators(capture.files["fullFrame"].toString()));
    blog(LOG_INFO, "[MK8DX QuickScore] saved result frame in %lld ms: %s",
         static_cast<long long>(capture.durationMs),
         capture.files["fullFrame"].toString().toUtf8().constData());
  } else {
    resultCapture_.insert("error", capture.error);
    captureStatus_->setText(
        tr("画像の自動保存に失敗しました。判定は続けます。\n%1").arg(capture.error));
    captureStatus_->setStyleSheet("color:#ffb74d;");
    captureStatus_->setToolTip({});
    blog(LOG_WARNING, "[MK8DX QuickScore] result image save failed: %s",
         capture.error.toUtf8().constData());
  }
  const bool useApi = apiEnabled_->isChecked();
  const QString readerName = useApi ? tr("Jev API") : tr("画像照合");
  auto completion = [this, generation, cycle, requestedAt, readerName, captured,
                     useApi](Analysis result) {
    if (!running_ || generation != generation_ || cycle != gate_.cycle())
      return;
    const auto current =
        latestImage_.scaled(1920, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32);
    if (!namesAgree(captured.image, current)) {
      result.error = tr("判定中に名前の画像が変化しました。下の結果を手動で確認してください。");
      for (auto &standing : result.standings) {
        standing.tag.clear();
        standing.confidence = 0;
      }
    }
    if (useApi) {
      resultCapture_.insert("apiModel", api_->lastModel);
      resultCapture_.insert("apiUsage", api_->lastUsage);
    }
    latest_ = std::move(result);
    const auto recognized =
        std::count_if(latest_.standings.begin(), latest_.standings.end(),
                      [](const Standing &standing) { return !standing.tag.empty(); });
    diagnostic_->setText(tr("%1 ｜ 順位 12/12 ｜ 先頭文字の一致 %2/12 ｜ %3 ms")
                             .arg(readerName)
                             .arg(recognized)
                             .arg(clock_.elapsed() - requestedAt));
    const QString error = latest_.error;
    const auto validation = session_.validate(latest_.standings);
    if (error.isEmpty() && validation.ok && autoCommit_->isChecked()) {
      if (commit(latest_.standings, false, latest_)) {
        setStatus(tr("%1の先頭文字・12順位・チーム人数を検証し集計しました。").arg(readerName));
        return;
      }
      const auto storageError = status_->text();
      review(latest_);
      setStatus(storageError + tr(" 保存先を確認し「確認した内容で集計」で再試行できます。"),
                "#ff8a80");
      return;
    }
    review(latest_);
    if (!error.isEmpty())
      setStatus(error + tr(" 下の12人のチームを選び「確認した内容で集計」を押してください。"),
                "#ffb74d");
    else if (!validation.ok)
      setStatus(tr("読み取れない文字またはチーム人数の不一致があります。下の結果を確認してくだ"
                   "さい。"),
                "#ffb74d");
    else
      setStatus(tr("%1の判定が完了しました。下の先頭文字とチームを確認して集計してください。")
                    .arg(readerName));
  };
  if (useApi) {
    setStatus(tr("Jev APIで12人の先頭文字を判定中です。停止で中断できます。"));
    api_->read(captured, session_.tags(), std::move(completion));
  } else
    completion(captured);
}
void MainDock::review(const Analysis &analysis) {
  if (analysis.standings.size() != 12)
    return;
  ++generation_;
  pending_ = analysis;
  reviewing_ = true;
  preview_->setPixmap(QPixmap::fromImage(pending_.image)
                          .scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  manual_->setEnabled(false);
  const auto activeTags = session_.tags().empty() ? configuredTags() : session_.tags();
  for (int row = 0; row < 12; ++row) {
    correction_->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
    auto *label = new QLabel;
    correction_->setCellWidget(row, 1, label);
    auto *box = new QComboBox;
    box->addItem(tr("未選択"), QString{});
    for (const auto &t : activeTags)
      box->addItem(QString::fromStdString(t), QString::fromStdString(t));

    // Unknown rows remain unselected; team capacity is validation, never evidence.
    std::string selectTag = pending_.standings[size_t(row)].tag;
    int idx = box->findData(QString::fromStdString(selectTag));
    box->setCurrentIndex(std::max(0, idx));
    correction_->setCellWidget(row, 2, box);
    correction_->setRowHeight(row, 42);
    updateRowPreview(row);
  }
  correction_->setColumnWidth(0, 40);
  correction_->setColumnWidth(2, 90);
  for (auto *w :
       std::initializer_list<QWidget *>{correction_, reviewHelp_, confirm_, cancel_, jevAssist_})
    w->show();
  setStatus(tr("名前の先頭文字と12人のチームを確認して集計してください。"), "#ffb74d");
}
void MainDock::updateRowPreview(int row) {
  auto *label = qobject_cast<QLabel *>(correction_->cellWidget(row, 1));
  if (!label)
    return;
  QImage image = Analyzer::rowImage(pending_.image, row);
  label->setPixmap(
      QPixmap::fromImage(image).scaled(300, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  if (size_t(row) < pending_.evidence.size()) {
    const auto &ev = pending_.evidence[size_t(row)];
    QString tip = ev.method;
    if (!ev.text.isEmpty())
      tip += "\n読み取り: " + ev.text;
    if (!ev.closestTag.empty())
      tip += QString("\n推定候補: %1 (%2%)")
                 .arg(QString::fromStdString(ev.closestTag))
                 .arg(int(ev.closestScore * 100));
    label->setToolTip(tip);
  }
}
void MainDock::confirmPending() {
  if (!reviewing_ || pending_.standings.size() != 12)
    return;
  auto standings = pending_.standings;
  for (int row = 0; row < 12; ++row) {
    const auto *box = qobject_cast<QComboBox *>(correction_->cellWidget(row, 2));
    standings[size_t(row)].tag = box->currentData().toString().toStdString();
    standings[size_t(row)].confidence = 1;
  }
  const auto validation = session_.validate(standings);
  if (!validation.ok) {
    QStringList errors;
    for (const auto &error : validation.errors)
      errors << QString::fromStdString(error);
    QMessageBox::warning(this, tr("確認してください"), errors.join("\n"));
    return;
  }
  if (!commit(standings, true, pending_))
    return;
  gate_.lock();
  cancelReview();
  setStatus(tr("確認済みの結果を集計しました。"));
}
void MainDock::cancelReview() {
  ++generation_;
  if (api_->busy())
    api_->cancel();
  reviewing_ = false;
  pending_ = {};
  confirm_->setEnabled(true);
  correction_->setEnabled(true);
  jevAssist_->setEnabled(true);
  for (auto *w :
       std::initializer_list<QWidget *>{correction_, reviewHelp_, confirm_, cancel_, jevAssist_})
    w->hide();
}
bool MainDock::commit(const std::vector<Standing> &standings, bool corrected, const Analysis &raw) {
  Session proposed = session_;
  if (!proposed.commit(standings, corrected))
    return false;
  auto audit = audit_;
  audit.append(
      QJsonObject{{"race", int(proposed.races().size())},
                  {"capturedAt", resultCapture_["frameReceivedAt"].toString(
                                     QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))},
                  {"committedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                  {"capture", resultCapture_},
                  {"corrected", corrected},
                  {"raw", evidenceJson(raw)}});
  QString error;
  if (!storeSession(configDirectory_ + "/session.json", proposed, audit, error)) {
    setStatus(tr("保存できないため加算を保留しました: %1").arg(error), "#ff8a80");
    return false;
  }
  session_ = std::move(proposed);
  audit_ = audit;
  if (corrected)
    learnConfirmed(raw, standings);
  updateScores();
  return true;
}
void MainDock::undo() {
  const bool invalidateSamples = !session_.races().empty() && session_.races().back().corrected;
  Session proposed = session_;
  if (!proposed.undo())
    return;
  auto audit = audit_;
  if (!audit.isEmpty())
    audit.removeLast();
  QString error;
  if (!storeSession(configDirectory_ + "/session.json", proposed, audit, error)) {
    setStatus(error, "#ff8a80");
    return;
  }
  session_ = std::move(proposed);
  audit_ = audit;
  if (invalidateSamples) {
    // A correction may have undone a mistaken manual label; don't reuse its samples.
    fastSamples_.clear();
    fastSource_.clear();
    ++generation_;
    QString sampleError;
    if (!saveSamples(configDirectory_ + "/fast-templates.json", fastSamples_, sampleError))
      fastStatus_->setText(tr("見本の取り消しを保存できません: %1").arg(sampleError));
    else
      updateFastStatus();
    saveSettings();
  }
  updateScores();
  setStatus(tr("直前のレースを取り消しました。同じ画面の自動再加算はしません。"));
}
void MainDock::resetSession() {
  if (QMessageBox::question(this, tr("集計リセット"),
                            tr("現在の集計をバックアップしてリセットしますか？")) !=
      QMessageBox::Yes)
    return;
  if (running_)
    startStop();
  const QString path = configDirectory_ + "/session.json";
  if (QFile::exists(path) &&
      !QFile::copy(path, path + "." +
                             QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") +
                             ".bak")) {
    setStatus(tr("バックアップできなかったため中止しました。"), "#ff8a80");
    return;
  }
  Session empty;
  empty.configure(configuredTags(), playersForFormat(format_->currentIndex()));
  QString error;
  if (!storeSession(path, empty, {}, error)) {
    setStatus(error, "#ff8a80");
    return;
  }
  session_ = std::move(empty);
  audit_ = {};
  updateScores();
  setStatus(tr("集計をリセットしました。次のレースを待機します。"));
}
void MainDock::updateFastStatus() {
  if (!fastStatus_)
    return;
  const auto tags = configuredTags();
  int learned = 0;
  if (fastSource_ == source_->currentData().toString())
    for (const auto &tag : tags)
      if (std::any_of(fastSamples_.begin(), fastSamples_.end(), [&](const TagSample &sample) {
            return initialIdentifier(QString::fromStdString(sample.tag)) ==
                   initialIdentifier(QString::fromStdString(tag));
          }))
        ++learned;
  fastStatus_->setText(
      learned == int(tags.size()) && learned > 0
          ? tr("%1チームの見本を登録済み。モデルの読み込みなしで画像照合します。\n"
               "名前の変更や不鮮明な画像は、12人の確認画面で保留します。")
                .arg(learned)
          : tr("見本登録 %1/%2チーム。最初のリザルトで12人のチームを確認してください。\n"
               "「確認した内容で集計」を押すと見本を保存し、次から画像照合で判定します。")
                .arg(learned)
                .arg(tags.size()));
}
void MainDock::learnConfirmed(const Analysis &raw, const std::vector<Standing> &standings) {
  auto samples = confirmedSamples(raw.image, standings);
  if (samples.empty())
    return;
  auto proposed =
      fastSource_ == source_->currentData().toString() ? fastSamples_ : std::vector<TagSample>{};
  proposed.insert(proposed.end(), samples.begin(), samples.end());
  if (proposed.size() > 24)
    proposed.erase(proposed.begin(), proposed.end() - 24);
  QString error;
  if (!saveSamples(configDirectory_ + "/fast-templates.json", proposed, error)) {
    fastStatus_->setText(tr("集計済みですが、名前の見本を保存できません: %1").arg(error));
    return;
  }
  fastSamples_ = std::move(proposed);
  fastSource_ = source_->currentData().toString();
  saveSettings();
  updateFastStatus();
}
void MainDock::updateScores() {
  const auto totals = session_.totals();
  const auto activeTags = session_.tags().empty() ? configuredTags() : session_.tags();
  const QString scoreLine = formatScoreLine(activeTags, totals, session_.races().size(), 12);
  scores_->setText(scoreLine);
  updateDiscord();

  {
    QFile file(configDirectory_ + "/score.txt");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      stream.setEncoding(QStringConverter::Utf8);
#else
      stream.setCodec("UTF-8");
#endif
      stream << scoreLine;
    }
  }

  QImage image(1920, 64, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  p.setBrush(QColor(15, 21, 31, 230));
  p.setPen(Qt::NoPen);
  p.drawRoundedRect(image.rect().adjusted(2, 2, -2, -2), 12, 12);

  QFont font = p.font();
  font.setPixelSize(28);
  font.setBold(true);
  p.setFont(font);
  p.setPen(QColor(255, 255, 255));
  p.drawText(QRect(20, 0, 1880, 64), Qt::AlignVCenter | Qt::AlignLeft, scoreLine);
  p.end();

  std::vector<uint8_t> pixels(image.constBits(),
                              image.constBits() + size_t(image.bytesPerLine() * image.height()));
  updateScoreboardImage(image.width(), image.height(), std::move(pixels));
}
void MainDock::saveSettings() {
  QSettings s(configDirectory_ + "/settings.ini", QSettings::IniFormat);
  s.setValue("schema", 9);
  s.setValue("sourceKey", source_->currentData());
  s.setValue("format", format_->currentIndex());
  s.setValue("tags", tags_->text());
  s.setValue("recognitionMode", apiEnabled_->isChecked() ? "jev-api" : "reference-templates");
  s.setValue("jevApiEnabled", apiEnabled_->isChecked());
  s.setValue("fastSource", fastSource_);
  s.setValue("jevApiAutoCommit", autoCommit_->isChecked());
  s.setValue("discordChannel", discordChannelId(discordChannel_->text()));
}
void MainDock::loadSettings() {
  QSettings s(configDirectory_ + "/settings.ini", QSettings::IniFormat);
  fastSource_ = s.value("fastSource").toString();
  apiEnabled_->setChecked(s.value("jevApiEnabled", true).toBool());
  format_->setCurrentIndex(std::clamp(s.value("format", 1).toInt(), 0, 3));
  tags_->setText(s.value("tags", "A, B, C, D").toString());
  savedSource_ = s.value("schema", 0).toInt() >= 5 ? s.value("sourceKey").toString() : QString{};
  autoCommit_->setChecked(s.value("jevApiAutoCommit", false).toBool());
  discordChannel_->setText(s.value("discordChannel").toString());
}

void MainDock::updateDiscord() {
  const bool connected = !loadStoredCredential(configDirectory_, "DiscordBot").isEmpty() &&
                         !discordChannelId(discordChannel_->text()).isEmpty();
  const bool sending = discord_ && discord_->busy();
  discordSend_->setEnabled(connected && !session_.races().empty() && !sending && !discordCooldown_);
  for (auto *widget : std::initializer_list<QWidget *>{discordToken_, discordChannel_, discordSave_,
                                                       discordDelete_})
    widget->setEnabled(!sending);
  QString reason;
  if (!connected)
    reason = tr("BotトークンとチャンネルIDを設定して「Discord設定を保存」を押してください。");
  else if (session_.races().empty())
    reason = tr("まだ集計がありません。リザルトを確定すると送信できます。");
  else
    reason = tr("現在の確定済み集計を送信します。");
  discordSend_->setToolTip(reason);
  if (discordStatus_->text().isEmpty() ||
      discordStatus_->text().startsWith(tr("まだ集計がありません")) ||
      discordStatus_->text().startsWith(tr("BotトークンとチャンネルIDを設定して")))
    discordStatus_->setText(reason);
}
void MainDock::sendDiscord() {
  if (discord_->busy() || discordCooldown_ || session_.races().empty())
    return;
  if (!QSslSocket::supportsSsl()) {
    discordStatus_->setText(
        tr("TLS機能を読み込めません。bin/64bit/tlsを含めて再インストールしてください。"));
    return;
  }
  const auto channel = discordChannelId(discordChannel_->text());
  const auto token = loadStoredCredential(configDirectory_, "DiscordBot");
  if (channel.isEmpty() || token.isEmpty()) {
    discordStatus_->setText(tr("BotトークンとチャンネルIDを設定して保存してください。"));
    return;
  }
  saveSettings();
  const auto content = scores_->text();
  discordStatus_->setText(tr("Discordへ送信中（最大15秒）…"));
  discord_->send(token, channel, content, [this](bool ok, const QString &message) {
    discordStatus_->setText(message);
    discordCooldown_ = true;
    updateDiscord();
    QTimer::singleShot(ok ? 2000 : 5000, this, [this] {
      discordCooldown_ = false;
      updateDiscord();
    });
  });
  updateDiscord();
}
void MainDock::setStatus(const QString &text, const QString &color) {
  status_->setText(text);
  status_->setStyleSheet("font-weight:600;color:" + color + ";");
}
void MainDock::exportDiagnostic() {
  const auto directory = QFileDialog::getExistingDirectory(this, tr("診断画像とJSONの保存先"));
  if (directory.isEmpty())
    return;
  const QString base =
      directory + "/quickscore-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
  const auto &a = reviewing_ ? pending_ : latest_;
  const auto image = a.image.isNull() ? latestImage_ : a.image;
  if (!image.isNull() && !image.save(base + ".png")) {
    setStatus(tr("診断画像を保存できません。"), "#ff8a80");
    return;
  }
  QSaveFile file(base + ".json");
  const auto data = QJsonDocument(QJsonObject{{"version", "0.8.1"},
                                              {"source", source_->currentText()},
                                              {"anchors", a.layout.anchors},
                                              {"pluses", a.layout.pluses},
                                              {"error", a.error},
                                              {"rows", evidenceJson(a)}})
                        .toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
    setStatus(tr("診断JSONを保存できません。"), "#ff8a80");
    return;
  }
  setStatus(tr("診断データを保存しました: %1").arg(base));
}
} // namespace quickscore
