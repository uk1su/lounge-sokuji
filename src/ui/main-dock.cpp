#include "main-dock.hpp"
#include "embedded-secrets.hpp"
#include "result-capture.hpp"
#include "scoreboard-source.hpp"
#include "session-store.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSslSocket>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <obs-module.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace quickscore {
namespace {
int playersForTeamCount(size_t teamCount) {
  if (teamCount == 2 || teamCount == 3 || teamCount == 4 || teamCount == 6)
    return 12 / int(teamCount);
  return 0;
}
QString formatNameForPlayers(int playersPerTeam) {
  switch (playersPerTeam) {
  case 2:
    return QStringLiteral("2v2");
  case 3:
    return QStringLiteral("3v3");
  case 4:
    return QStringLiteral("4v4");
  case 6:
    return QStringLiteral("6v6");
  default:
    return QString::number(playersPerTeam) + "v" + QString::number(playersPerTeam);
  }
}
QWidget *buttons(std::initializer_list<QWidget *> widgets) {
  auto *row = new QWidget;
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  for (auto *w : widgets)
    layout->addWidget(w);
  return row;
}

class SourceComboBox final : public QComboBox {
public:
  explicit SourceComboBox(std::function<void()> onBeforePopup, QWidget *parent = nullptr)
      : QComboBox(parent), onBeforePopup_(std::move(onBeforePopup)) {}

  void showPopup() override {
    if (onBeforePopup_)
      onBeforePopup_();
    QComboBox::showPopup();
  }

private:
  std::function<void()> onBeforePopup_;
};
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
QImage cropResultArea(const QImage &frame) {
  if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0)
    return {};
  const double aspect = double(frame.width()) / double(frame.height());
  if (std::abs(aspect - 16.0 / 9.0) > 0.03)
    return frame;
  const QRect ref = resultTableRect();
  if (frame.size() == QSize(1920, 1080))
    return frame.copy(ref);
  const QRect scaled(qRound(frame.width() * double(ref.x()) / 1920.0),
                     qRound(frame.height() * double(ref.y()) / 1080.0),
                     qRound(frame.width() * double(ref.width()) / 1920.0),
                     qRound(frame.height() * double(ref.height()) / 1080.0));
  return frame.copy(scaled.intersected(frame.rect()));
}
QColor colorForTeamTag(const std::string &tag, const std::vector<std::string> &teamTags) {
  if (tag.empty())
    return QColor(127, 29, 29);
  static const QColor kPalette[]{
      QColor(25, 118, 210), // Blue (Player team default)
      QColor(198, 40, 40),  // Red
      QColor(46, 125, 50),  // Green
      QColor(239, 108, 0),  // Orange
      QColor(123, 31, 162), // Purple
      QColor(0, 131, 143),  // Teal
  };
  for (size_t i = 0; i < teamTags.size(); ++i) {
    if (teamTags[i] == tag)
      return kPalette[i % std::size(kPalette)];
  }
  return QColor(69, 90, 100);
}
} // namespace

ResultPreviewWidget::ResultPreviewWidget(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);
  setMinimumWidth(180);
  setMaximumHeight(320);
}

QSize ResultPreviewWidget::sizeHint() const {
  return QSize(270, heightForWidth(270));
}

QSize ResultPreviewWidget::minimumSizeHint() const {
  return QSize(180, cropped_.isNull() ? 48 : heightForWidth(180));
}

int ResultPreviewWidget::heightForWidth(int width) const {
  if (cropped_.isNull())
    return 48;
  const double logicalW = (standings_.size() == 12) ? 1205.0 : double(std::max(1, cropped_.width()));
  const double logicalH = (standings_.size() == 12) ? 945.0 : double(std::max(1, cropped_.height()));
  const int w = std::max(180, width - 6);
  const int h = int(std::lround(w * (logicalH / logicalW))) + 6;
  return std::clamp(h, 150, 320);
}

void ResultPreviewWidget::setPlaceholderText(const QString &text) {
  placeholder_ = text;
  if (cropped_.isNull())
    update();
}

void ResultPreviewWidget::setPreview(const QImage &frame, const std::vector<Standing> &standings,
                                     const std::vector<std::string> &teamTags,
                                     const std::vector<RowEvidence> &evidence,
                                     const StandingsScoreRead &cumulative) {
  const bool wasEmpty = cropped_.isNull();
  const bool badgeChanged = (standings_.size() == 12) != (standings.size() == 12);
  QImage cropped = cropResultArea(frame);
  // During live monitoring (no standings overlay), downscale preview once to keep UI paint cheap.
  if (standings.empty() && !cropped.isNull() && cropped.width() > 360) {
    cropped = cropped.scaledToWidth(360, Qt::FastTransformation);
  }
  cropped_ = std::move(cropped);
  standings_ = standings;
  teamTags_ = teamTags;
  evidence_ = evidence;
  cumulative_ = cumulative;
  if (wasEmpty != cropped_.isNull() || badgeChanged)
    updateGeometry();
  update();
}

void ResultPreviewWidget::setEditing(bool editing, bool cumulativeMode) {
  editing_ = editing;
  cumulativeMode_ = cumulativeMode;
  hoveredRow_ = -1;
  setCursor(Qt::ArrowCursor);
  update();
}

void ResultPreviewWidget::setRowTag(int row, const QString &tag) {
  if (row < 0 || size_t(row) >= standings_.size())
    return;
  standings_[size_t(row)].tag = tag.toStdString();
  standings_[size_t(row)].confidence = tag.isEmpty() ? 0.0 : 1.0;
  update();
  if (onEdited_)
    onEdited_();
}

QString ResultPreviewWidget::rowTag(int row) const {
  if (row < 0 || size_t(row) >= standings_.size())
    return {};
  return QString::fromStdString(standings_[size_t(row)].tag);
}

int ResultPreviewWidget::hitRow(const QPoint &pos) const {
  if (!editing_ || standings_.size() != 12)
    return -1;
  for (int row = 0; row < 12; ++row) {
    if (badgeRects_[size_t(row)].contains(pos))
      return row;
  }
  return -1;
}

QString ResultPreviewWidget::tooltipForRow(int row) const {
  if (row < 0 || row >= 12)
    return {};
  QStringList lines;
  lines << QStringLiteral("%1位").arg(row + 1);
  if (cumulativeMode_ && cumulative_.rows[size_t(row)].ok())
    lines[0] += QStringLiteral(" (%1点)").arg(cumulative_.rows[size_t(row)].points);
  if (size_t(row) < evidence_.size()) {
    const auto &ev = evidence_[size_t(row)];
    if (!ev.method.isEmpty())
      lines << ev.method;
    if (!ev.text.isEmpty())
      lines << QStringLiteral("詳細: %1").arg(ev.text);
  }
  if (editing_)
    lines << tr("クリックしてチームタグを変更");
  return lines.join('\n');
}

void ResultPreviewWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.setRenderHint(QPainter::TextAntialiasing);

  p.fillRect(rect(), QColor(21, 25, 31));
  p.setPen(QColor(66, 72, 80));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (cropped_.isNull()) {
    drawRect_ = {};
    badgeRects_.fill(QRect{});
    p.setPen(QColor(210, 216, 224));
    p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
    return;
  }

  const bool showBadges = (standings_.size() == 12);
  constexpr int kCropW = 1040;
  constexpr int kCropH = 945;
  constexpr int kTagPanelW = 165;
  const double logicalW = showBadges ? double(kCropW + kTagPanelW) : double(cropped_.width());
  const double logicalH = showBadges ? double(kCropH) : double(cropped_.height());

  const QRect avail = rect().adjusted(3, 3, -3, -3);
  if (avail.width() <= 0 || avail.height() <= 0)
    return;
  const double scale = std::min(avail.width() / logicalW, avail.height() / logicalH);
  const int drawW = std::max(1, int(std::lround(logicalW * scale)));
  const int drawH = std::max(1, int(std::lround(logicalH * scale)));
  const int drawX = avail.left() + (avail.width() - drawW) / 2;
  const int drawY = avail.top() + (avail.height() - drawH) / 2;
  drawRect_ = QRect(drawX, drawY, drawW, drawH);

  if (!showBadges) {
    badgeRects_.fill(QRect{});
    p.drawImage(drawRect_, cropped_);
    return;
  }

  const int cropDrawW = int(std::lround(drawW * (double(kCropW) / logicalW)));
  const QRect cropTarget(drawX, drawY, cropDrawW, drawH);
  const QRect stripTarget(drawX + cropDrawW, drawY, drawW - cropDrawW, drawH);
  p.drawImage(cropTarget, cropped_);
  p.fillRect(stripTarget, QColor(22, 26, 32));
  p.setPen(QColor(56, 65, 78));
  p.drawLine(stripTarget.left(), stripTarget.top(), stripTarget.left(), stripTarget.bottom());

  const double sx = double(drawW) / logicalW;
  const double sy = double(drawH) / logicalH;
  QFont font = p.font();
  font.setPixelSize(std::max(9, int(std::lround(34.0 * sy))));
  font.setBold(true);
  p.setFont(font);

  for (int row = 0; row < 12; ++row) {
    const double centerLogicalY = 112.0 + 78.0 * row - 70.0;
    const int bx = int(std::lround(drawX + (kCropW + 8.0) * sx));
    const int by = int(std::lround(drawY + (centerLogicalY - 28.0) * sy));
    const int bw = std::max(22, int(std::lround((kTagPanelW - 16.0) * sx)));
    const int bh = std::max(12, int(std::lround(56.0 * sy)));
    const QRect badge(bx, by, bw, bh);
    badgeRects_[size_t(row)] = badge;

    const std::string &tag = standings_[size_t(row)].tag;
    QColor bg = colorForTeamTag(tag, teamTags_);
    if (editing_ && hoveredRow_ == row)
      bg = bg.lighter(120);
    p.setBrush(bg);

    if (editing_ && hoveredRow_ == row)
      p.setPen(QPen(Qt::white, 2.0));
    else if (tag.empty())
      p.setPen(QPen(QColor(255, 183, 77), editing_ ? 2.0 : 1.5));
    else
      p.setPen(QPen(bg.lighter(editing_ ? 145 : 130), editing_ ? 1.5 : 1.0));

    const qreal radius = std::max(3.0, 12.0 * sy);
    p.drawRoundedRect(badge, radius, radius);

    p.setPen(Qt::white);
    QString label = tag.empty() ? QStringLiteral("?") : QString::fromStdString(tag);
    if (cumulativeMode_ && cumulative_.rows[size_t(row)].ok())
      label = QString("%1 %2").arg(cumulative_.rows[size_t(row)].points).arg(label);
    if (editing_)
      label += QStringLiteral(" ▾");
    p.drawText(badge.adjusted(3, 0, -3, 0), Qt::AlignCenter,
               p.fontMetrics().elidedText(label, Qt::ElideRight, std::max(10, badge.width() - 6)));
  }
}

void ResultPreviewWidget::mouseMoveEvent(QMouseEvent *event) {
  const int row = hitRow(event->pos());
  if (row != hoveredRow_) {
    hoveredRow_ = row;
    update();
  }
  setCursor(editing_ && row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
  if (row >= 0) {
    const QString tip = tooltipForRow(row);
    if (!tip.isEmpty())
      QToolTip::showText(event->globalPosition().toPoint(), tip, this);
  }
  QWidget::mouseMoveEvent(event);
}

void ResultPreviewWidget::leaveEvent(QEvent *event) {
  if (hoveredRow_ >= 0) {
    hoveredRow_ = -1;
    update();
  }
  setCursor(Qt::ArrowCursor);
  QWidget::leaveEvent(event);
}

void ResultPreviewWidget::mousePressEvent(QMouseEvent *event) {
  if (editing_ && event->button() == Qt::LeftButton) {
    const int row = hitRow(event->pos());
    if (row >= 0) {
      QMenu menu(this);
      auto *clearAct = menu.addAction(tr("未選択"));
      clearAct->setData(QString{});
      for (const auto &t : teamTags_) {
        const QString qtag = QString::fromStdString(t);
        QPixmap swatch(14, 14);
        swatch.fill(colorForTeamTag(t, teamTags_));
        auto *act = menu.addAction(QIcon(swatch), qtag);
        act->setData(qtag);
      }
      QAction *picked = menu.exec(mapToGlobal(badgeRects_[size_t(row)].bottomLeft()));
      if (picked)
        setRowTag(row, picked->data().toString());
      return;
    }
  }
  QWidget::mousePressEvent(event);
}

MainDock::MainDock(QString directory, QWidget *parent, QUrl testDiscordEndpoint,
                   QUrl testApiEndpoint)
    : QWidget(parent), configDirectory_(std::move(directory)),
      hasTestDiscordEndpoint_(!testDiscordEndpoint.isEmpty()) {
  clock_.start();
  setMinimumWidth(230);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  outer->addWidget(scroll);
  auto *body = new QWidget;
  body->setStyleSheet(
      "QPushButton{padding:2px 6px;font-size:11.5px;min-height:19px;}"
      "QComboBox,QLineEdit{padding:1px 5px;font-size:11.5px;min-height:19px;}"
      "QLabel{font-size:11.5px;}"
      "QCheckBox{font-size:11.5px;}");
  scroll->setWidget(body);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(5, 4, 5, 4);
  layout->setSpacing(3);

  auto *setup = new QFrame;
  setup->setStyleSheet("QFrame#setupPanel{border:1px solid #3c4552;border-radius:4px;}");
  setup->setObjectName("setupPanel");
  auto *form = new QGridLayout(setup);
  form->setContentsMargins(5, 4, 5, 4);
  form->setSpacing(4);
  source_ = new SourceComboBox([this] {
    if (!running_)
      refreshSources();
  });
  source_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  source_->setMinimumContentsLength(5);
  source_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  refresh_ = new QPushButton(tr("更新"));
  refresh_->setToolTip(tr("映像ソース一覧を更新"));
  refresh_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  tags_ = new QLineEdit("A, B, C, D");
  tags_->setPlaceholderText(tr("左端=自チーム (例: M, う, A, L, B, R)"));
  tags_->setToolTip(tr("一番左に自分のチームタグを入れてください (例: M, う, A, L, B, R)"));
  tags_->setMinimumWidth(60);
  detectedFormat_ = new QLabel(tr("3v3"));
  detectedFormat_->setObjectName("detectedFormat");
  detectedFormat_->setAlignment(Qt::AlignCenter);
  detectedFormat_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  detectedFormat_->setStyleSheet(
      "font-size:11px;font-weight:700;color:#80cbc4;background:#1e2630;border:1px solid "
      "#3c4552;border-radius:3px;padding:1px 6px;");
  auto *srcLbl = new QLabel(tr("映像"));
  srcLbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto *tagLbl = new QLabel(tr("タグ"));
  tagLbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto *userLbl = new QLabel(tr("ユーザーID"));
  userLbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  discordUserId_ = new QLineEdit;
  discordUserId_->setObjectName("discordUserId");
  discordUserId_->setPlaceholderText(tr("Discordユーザー名 (例: uk.i) または数字ID"));
  discordUserId_->setToolTip(
      tr("Discordの@ユーザー名（例: uk.i）または数字のユーザーIDを入力すると、専用スレッド作成と個人オーバーレイが有効になります"));
  discordUserId_->setMinimumWidth(60);
  form->addWidget(srcLbl, 0, 0);
  form->addWidget(source_, 0, 1);
  form->addWidget(refresh_, 0, 2);
  form->addWidget(tagLbl, 1, 0);
  form->addWidget(tags_, 1, 1);
  form->addWidget(detectedFormat_, 1, 2);
  form->addWidget(userLbl, 2, 0);
  form->addWidget(discordUserId_, 2, 1, 1, 2);
  form->setColumnStretch(1, 1);
  layout->addWidget(setup);

  api_ = std::make_unique<JevApiReader>(configDirectory_, testApiEndpoint);

  start_ = new QPushButton(tr("集計開始"));
  stop_ = new QPushButton(tr("集計停止"));
  stop_->setEnabled(false);
  auto *reset = new QPushButton(tr("リセット"));
  reset->setObjectName("reset");
  reset->setToolTip(tr("集計リセット"));
  layout->addWidget(buttons({start_, stop_, reset}));

  status_ = new QLabel;
  status_->setWordWrap(true);
  status_->setMinimumWidth(120);
  status_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  layout->addWidget(status_);

  diagnostic_ = new QLabel(tr("映像待ち"));
  diagnostic_->setWordWrap(true);
  diagnostic_->setMinimumWidth(120);
  diagnostic_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  diagnostic_->setStyleSheet("font-size:10.5px;color:#9aa4b2;");
  layout->addWidget(diagnostic_);

  courseLabel_ = new QLabel;
  courseLabel_->setWordWrap(true);
  courseLabel_->setMinimumWidth(120);
  courseLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  courseLabel_->setStyleSheet(
      "font-size:11.5px;font-weight:700;color:#80cbc4;background:#10141a;border:1px solid "
      "#2b3842;border-radius:4px;padding:3px 7px;");
  japaneseOcrAvailable_ = CourseRecognizer::isJapaneseOcrAvailable();
  if (!japaneseOcrAvailable_) {
    blog(LOG_WARNING,
         "[MK8DX QuickScore] Windows Japanese OCR language pack is not installed; course "
         "auto-recognition is unavailable.");
    courseLabel_->setToolTip(
        tr("Windowsの日本語言語パック(OCR)が未導入のため、コース自動認識が利用できません。"
           "Windowsの設定→時刻と言語→言語から日本語を追加してください。"));
  }
  if (!QSslSocket::supportsSsl()) {
    blog(LOG_WARNING,
         "[MK8DX QuickScore] Qt TLS backend is unavailable; HTTPS API/Discord requests may fail "
         "(check OBS Qt version compatibility with qschannelbackend.dll).");
  }
  updateCourseDisplay();
  layout->addWidget(courseLabel_);

  preview_ = new ResultPreviewWidget;
  preview_->setPlaceholderText(tr("集計開始後、ここにリザルトを表示します"));
  preview_->setOnEdited([this] {
    userTouchedReview_ = true;
    reviewOpenedMs_ = clock_.elapsed();
  });
  layout->addWidget(preview_);

  reviewHelp_ = new QLabel(tr("右のバッジをクリックしてチームを選択してください。"));
  reviewHelp_->setWordWrap(true);
  reviewHelp_->setMinimumWidth(120);
  reviewHelp_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  reviewHelp_->setStyleSheet("font-size:10.5px;color:#cfd8dc;");
  reviewHelp_->hide();
  layout->addWidget(reviewHelp_);

  reviewCourse_ = new QLineEdit;
  reviewCourse_->setObjectName("reviewCourse");
  reviewCourse_->setClearButtonEnabled(true);
  reviewCourse_->setPlaceholderText(tr("コース名（例: どるみ、マリカス）"));
  reviewCourse_->setToolTip(tr("略称・プレイヤー呼称（どるみ等）を入力すると正式名称へ自動補正します"));
  reviewCourse_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto *courseEditLbl = new QLabel(tr("コース"));
  courseEditLbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  reviewCourseRow_ = buttons({courseEditLbl, reviewCourse_});
  reviewCourseRow_->hide();
  layout->addWidget(reviewCourseRow_);

  confirm_ = new QPushButton(tr("この内容で集計する"));
  confirm_->setToolTip(tr("選んだチームとコースで点数を加算し、Discordへ送信します"));
  confirm_->setStyleSheet(
      "QPushButton{background:#2e7d32;color:#ffffff;font-weight:700;padding:5px 8px;"
      "border:1px solid #43a047;border-radius:4px;font-size:12px;}"
      "QPushButton:hover{background:#388e3c;}"
      "QPushButton:disabled{background:#263238;color:#78909c;border-color:#37474f;}");
  confirm_->hide();
  layout->addWidget(confirm_);

  const QString subBtnStyle =
      "QPushButton{background:transparent;color:#9aa4b2;border:none;font-size:10.5px;padding:1px 6px;}"
      "QPushButton:hover{color:#eceff1;text-decoration:underline;}"
      "QPushButton:disabled{color:#546e7a;}";
  cancel_ = new QPushButton(tr("保留して次へ"));
  cancel_->setToolTip(tr("このレースをDiscordに「保留」と表示して次のレースへ進みます"));
  cancel_->setStyleSheet(subBtnStyle);
  retryRead_ = new QPushButton(tr("AI再読取"));
  retryRead_->setObjectName("retryRead");
  retryRead_->setToolTip(tr("同じ画像をもう一度AIで読み直します（手動で直した行は維持）"));
  retryRead_->setStyleSheet(subBtnStyle);
  reviewSubRow_ = buttons({cancel_, retryRead_});
  reviewSubRow_->hide();
  layout->addWidget(reviewSubRow_);

  scores_ = new QLabel;
  scores_->setWordWrap(true);
  scores_->setMinimumWidth(120);
  scores_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  scores_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  scores_->setStyleSheet(
      "font-size:12.5px;font-weight:700;color:#eceff1;background:#10141a;border:1px solid "
      "#37474f;border-radius:4px;padding:4px 7px;");
  layout->addWidget(scores_);

  postDiscord_ = new QCheckBox(tr("discordの即時チャンネルにも投稿する"));
  postDiscord_->setObjectName("postDiscord");
  layout->addWidget(postDiscord_);

  discord_ = std::make_unique<DiscordSender>(nullptr, testDiscordEndpoint);
  connect(postDiscord_, &QCheckBox::clicked, this, [this](bool checked) {
    saveSettings();
    if (checked && (running_ || !session_.races().empty()))
      syncDiscord(!session_.races().empty());
  });

  raceSelector_ = new QComboBox;
  raceSelector_->setObjectName("raceHistory");
  raceSelector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  raceSelector_->setMinimumContentsLength(6);
  raceSelector_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  raceSelector_->setToolTip(tr("過去のレースを選ぶと、そのまま修正画面が開きます"));
  layout->addWidget(raceSelector_);

  layout->addStretch();

  connect(refresh_, &QPushButton::clicked, this, [this] { refreshSources(); });
  connect(tags_, &QLineEdit::textChanged, this, [this] { updateDetectedFormat(); });
  connect(tags_, &QLineEdit::editingFinished, this, [this] {
    if (session_.tags().empty()) {
      saveSettings();
      return;
    }
    const auto newTags = configuredTags();
    if (newTags == session_.tags())
      return;
    if (newTags.size() == session_.tags().size() && session_.renameTags(newTags)) {
      for (auto &s : latest_.standings) {
        if (!s.tag.empty() &&
            std::find(newTags.begin(), newTags.end(), s.tag) == newTags.end()) {
          s.tag.clear();
        }
      }
      saveSettings();
      updateScores();
      if (running_ || !session_.races().empty())
        syncDiscord(!session_.races().empty());
      setStatus(tr("タグを変更しました（既存の点数を引き継ぎました）"));
    } else if (session_.races().empty() && !session_.hasPendingFirstRace() &&
               playersForTeamCount(newTags.size()) > 0) {
      session_.configure(newTags, playersForTeamCount(newTags.size()));
      saveSettings();
      updateScores();
      if (running_)
        syncDiscord(false);
    } else {
      QStringList prev;
      for (const auto &t : session_.tags())
        prev << QString::fromStdString(t);
      const QSignalBlocker blocker(tags_);
      tags_->setText(prev.join(", "));
      updateDetectedFormat();
      setStatus(tr("集計中はチーム数を変更できません（タグ名の変更のみ可能です）"), "#ffb74d");
    }
  });
  connect(discordUserId_, &QLineEdit::editingFinished, this, [this] {
    const QString cleaned = configuredDiscordUserId();
    if (!cleaned.isEmpty() && cleaned != discordUserId_->text().trimmed()) {
      const QSignalBlocker blocker(discordUserId_);
      discordUserId_->setText(cleaned);
    }
    saveSettings();
  });
  connect(reviewCourse_, &QLineEdit::textEdited, this, [this] {
    if (reviewing_) {
      userTouchedReview_ = true;
      reviewOpenedMs_ = clock_.elapsed();
    }
  });
  connect(reviewCourse_, &QLineEdit::editingFinished, this, [this] {
    if (!reviewCourse_)
      return;
    const QString resolved = editedReviewCourse();
    if (resolved != reviewCourse_->text().trimmed()) {
      const QSignalBlocker blocker(reviewCourse_);
      reviewCourse_->setText(resolved);
    }
  });
  connect(start_, &QPushButton::clicked, this, [this] { startAggregation(); });
  connect(stop_, &QPushButton::clicked, this, [this] { stopAggregation(); });
  connect(confirm_, &QPushButton::clicked, this, [this] { confirmPending(); });
  connect(retryRead_, &QPushButton::clicked, this, [this] { requestRetryRecognition(); });
  connect(cancel_, &QPushButton::clicked, this, [this] {
    const auto prevMode = editMode_;
    QString skippedCourse;
    int skippedRaceNo = 0;
    if (prevMode == EditMode::NewRace) {
      gate_.lock();
      skippedRaceNo = int(session_.effectiveRaceCount()) + 1;
      skippedCourse = editedReviewCourse();
      if (skippedCourse.isEmpty())
        skippedCourse = !pending_.detectedCourse.isEmpty() ? pending_.detectedCourse : currentCourse_;
      auto it = std::find_if(heldRaces_.begin(), heldRaces_.end(),
                             [skippedRaceNo](const auto &item) { return item.first == skippedRaceNo; });
      if (it != heldRaces_.end())
        it->second = skippedCourse;
      else
        heldRaces_.emplace_back(skippedRaceNo, skippedCourse);
      clearCurrentCourse();
      updateScores();
    }
    cancelReview();
    if (prevMode == EditMode::NewRace) {
      if (postDiscord_->isChecked())
        syncDiscord(true);
      setStatus(skippedCourse.isEmpty()
                    ? tr("第%1レースを保留（スキップ）し、次のレースを待機します。").arg(skippedRaceNo)
                    : tr("第%1レース（%2）を保留（スキップ）し、次のレースを待機します。")
                          .arg(skippedRaceNo)
                          .arg(skippedCourse));
    } else if (prevMode == EditMode::CumulativeReconcile) {
      setStatus(tr("総合順位表の検算をスキップしました。"));
    } else {
      setStatus(tr("修正をキャンセルしました。"));
    }
  });
  connect(reset, &QPushButton::clicked, this, [this] { resetSession(); });
  connect(raceSelector_, QOverload<int>::of(&QComboBox::activated), this,
          [this](int) { openSelectedRaceEditor(); });

  preview_->setObjectName("preview");
  source_->setObjectName("source");
  tags_->setObjectName("tags");
  start_->setObjectName("start");
  stop_->setObjectName("stop");
  scores_->setObjectName("scores");
  status_->setObjectName("status");
  diagnostic_->setObjectName("diagnostic");
  confirm_->setObjectName("confirm");
  cancel_->setObjectName("cancel");

  loadSettings();
  refreshSources();
  QTimer::singleShot(1500, this, [this] {
    if (!running_ && source_ && source_->count() <= 1)
      refreshSources();
  });

  QString overrideError;
  if (!loadSamples(configDirectory_ + "/player-overrides.json", playerOverrides_, overrideError))
    playerOverrides_.clear();

  QString error;
  if (!loadSession(configDirectory_ + "/session.json", session_, audit_, error)) {
    storageOk_ = false;
    setStatus(error, "#ff8a80");
  } else {
    if (!session_.races().empty()) {
      QStringList tags;
      for (const auto &s : session_.tags())
        tags << QString::fromStdString(s);
      tags_->setText(tags.join(", "));
    }
    updateDetectedFormat();
    setStatus(tr("準備完了。開始後、リザルト以外の画面を3秒映すと自動集計を待機します。"));
  }

  char *path = obs_module_file("templates.mkr");
  const QString templatesPath = QString::fromUtf8(path ? path : "");
  try {
    if (!path)
      throw std::runtime_error("templates.mkr missing");
    analyzer_ = std::make_unique<Analyzer>(templatesPath);
  } catch (const std::exception &e) {
    setStatus(tr("認識テンプレートを読み込めません: %1").arg(QString::fromUtf8(e.what())),
              "#ff8a80");
    start_->setEnabled(false);
  }
  bfree(path);

  overlayServer_ = std::make_unique<OverlayServer>(this);
  char *overlayPathRaw = obs_module_file("overlay.html");
  const QString overlayPath =
      overlayPathRaw ? QString::fromUtf8(overlayPathRaw)
                     : (templatesPath.isEmpty()
                            ? QString()
                            : QFileInfo(templatesPath).dir().filePath("overlay.html"));
  if (overlayPathRaw)
    bfree(overlayPathRaw);
  if (overlayServer_->loadHtmlFile(overlayPath) && !configDirectory_.isEmpty()) {
    QFile outHtml(configDirectory_ + "/overlay.html");
    if (outHtml.open(QIODevice::WriteOnly | QIODevice::Truncate))
      outHtml.write(overlayServer_->htmlContent());
  }
  if (overlayServer_->start(hasTestDiscordEndpoint_ ? 0 : 18421)) {
    blog(LOG_INFO, "[MK8DX QuickScore] Overlay HTTP URL: http://127.0.0.1:%u/overlay.html",
         unsigned(overlayServer_->port()));
  }

  capture_ = std::make_unique<FrameCapture>(
      [this](Frame frame) {
        if (frameQueued_.exchange(true))
          return;
        QMetaObject::invokeMethod(
            this,
            [this, frame = std::move(frame)]() mutable {
              frameQueued_ = false;
              receiveFrame(std::move(frame));
            },
            Qt::QueuedConnection);
      },
      [this]() {
        return !frameQueued_.load(std::memory_order_relaxed) &&
               !workerBusy_.load(std::memory_order_relaxed);
      });
  worker_ = std::thread([this] { workerLoop(); });
  auto *timer = new QTimer(this);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, [this] {
    if (!running_)
      return;
    if (reviewing_ &&
        (editMode_ == EditMode::ExistingRace || editMode_ == EditMode::PendingFirstRace) &&
        !retryInFlight_ && (clock_.elapsed() - reviewOpenedMs_) >= 60000) {
      cancelReview();
      if (!latestImage_.isNull())
        updatePreview(latestImage_);
      setStatus(tr("編集画面を1分間操作しなかったため、ライブ監視へ戻りました。"));
      return;
    }
    if (reviewing_)
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
  refreshRaceHistory();
}

MainDock::~MainDock() {
  running_ = false;
  ++generation_;
  api_->cancel();
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

std::vector<std::string> MainDock::configuredTags() const {
  static const QRegularExpression kTagSep(QStringLiteral("[,，、\\s]+"));
  std::vector<std::string> out;
  for (const auto &p : tags_->text().split(kTagSep, Qt::SkipEmptyParts)) {
    auto v = p.trimmed().toStdString();
    if (!v.empty() && std::find(out.begin(), out.end(), v) == out.end())
      out.push_back(v);
  }
  return out;
}

int MainDock::configuredPlayersPerTeam() const {
  return playersForTeamCount(configuredTags().size());
}

QString MainDock::configuredDiscordUserId() const {
  if (!discordUserId_)
    return {};
  QString raw = discordUserId_->text().trimmed();
  if (raw.startsWith("<@") && raw.endsWith('>')) {
    raw = raw.mid(2, raw.size() - 3);
    if (raw.startsWith('!'))
      raw = raw.mid(1);
    raw = raw.trimmed();
  }
  while (raw.startsWith('@'))
    raw.remove(0, 1);
  raw = raw.trimmed();
  if (raw.isEmpty() || raw.size() > 64)
    return {};
  for (const QChar ch : raw) {
    if (ch.isSpace())
      return {};
  }
  return raw;
}

void MainDock::updateDetectedFormat() {
  if (!detectedFormat_)
    return;
  const int players = configuredPlayersPerTeam();
  if (players > 0) {
    detectedFormat_->setText(formatNameForPlayers(players));
    detectedFormat_->setStyleSheet(
        "font-size:11px;font-weight:700;color:#80cbc4;background:#1e2630;border:1px solid "
        "#3c4552;border-radius:3px;padding:1px 6px;");
  } else {
    detectedFormat_->setText(tr("タグ数%1").arg(int(configuredTags().size())));
    detectedFormat_->setStyleSheet(
        "font-size:11px;font-weight:700;color:#ff8a80;background:#2b1b1d;border:1px solid "
        "#7f3337;border-radius:3px;padding:1px 6px;");
  }
  if (scores_ && session_.races().empty() && !session_.hasPendingFirstRace())
    updateScores();
}

void MainDock::stopAggregation(bool autoFinished, bool syncOverlay) {
  if (!running_)
    return;
  running_ = false;
  if (autoFinished) {
    loungeOverlayActive_ = true;
    if (!loungeOverlayUntilUtc_.isValid())
      loungeOverlayUntilUtc_ = QDateTime::currentDateTimeUtc().addSecs(60);
  } else {
    loungeOverlayActive_ = false;
    loungeOverlayUntilUtc_ = {};
  }
  ++generation_;
  api_->cancel();
  capture_->stop();
  gate_.pause();
  clearCurrentCourse();
  standingsRequestActive_ = false;
  standingsStableCount_ = 0;
  standingsLastTotal_ = -1;
  standingsStableRows_.fill(-1);
  cancelReview();
  start_->setEnabled(analyzer_ != nullptr);
  stop_->setEnabled(false);
  for (auto *w : std::initializer_list<QWidget *>{source_, tags_, refresh_})
    w->setEnabled(true);
  setStatus(tr("停止中"));
  if (syncOverlay)
    syncDiscord(false);
}

bool MainDock::checkAutoFinish(bool syncOverlay) {
  if (!running_ || session_.effectiveRaceCount() < 12)
    return false;
  loungeOverlayActive_ = true;
  if (!loungeOverlayUntilUtc_.isValid())
    loungeOverlayUntilUtc_ = QDateTime::currentDateTimeUtc().addSecs(60);
  stopAggregation(true, syncOverlay);
  setStatus(tr("12レース終了したため自動停止しました。"));
  return true;
}

void MainDock::startAggregation() {
  if (running_)
    return;
  if (!storageOk_) {
    QMessageBox::warning(this, tr("保存データの確認"),
                         tr("保存データに読み込みエラーがあります。元ファイルを退避・確認してOBSを"
                            "再起動してください。"));
    return;
  }
  if (!api_->configurationError().isEmpty()) {
    setStatus(api_->configurationError(), "#ffb74d");
    return;
  }
  auto tags = configuredTags();
  const int players = playersForTeamCount(tags.size());
  if (players <= 0) {
    QMessageBox::warning(
        this, tr("設定エラー"),
        tr("タグ数は 2(6v6)・3(4v4)・4(3v3)・6(2v2) のいずれかになるよう入力してください。"));
    return;
  }
  QString tagError;
  if (!validateTeamTags(tags, &tagError)) {
    QMessageBox::warning(this, tr("設定エラー"), tagError);
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
      setStatus(tr("映像ソースが見つかりません。更新してください。"), "#ff8a80");
      return;
    }
  }
  session_.configure(tags, players);
  ++generation_;
  gate_.pause();
  courseGate_.reset();
  currentCourse_.clear();
  updateCourseDisplay();
  requestIssued_ = false;
  requestCycle_ = gate_.cycle();
  latest_ = {};
  latestImage_ = {};
  lastCommittedFrame_ = {};
  lastCommittedStandings_.clear();
  standingsRequestActive_ = false;
  standingsStableCount_ = 0;
  standingsLastTotal_ = -1;
  standingsStableRows_.fill(-1);
  latestFrameReceivedAt_ = {};
  resultCapture_ = {};
  capture_->setSource(selected);
  if (selected)
    obs_source_release(selected);
  running_ = true;
  loungeOverlayActive_ = true;
  loungeOverlayUntilUtc_ = {};
  lastFrameMs_ = lastAnalysisMs_ = clock_.elapsed();
  lastPreviewMs_ = 0;
  capture_->setIntervalSeconds(0.250f);
  capture_->start();
  for (auto *w : std::initializer_list<QWidget *>{source_, refresh_})
    w->setEnabled(false);
  start_->setEnabled(false);
  stop_->setEnabled(true);
  setStatus(tr("映像を確認中（リザルト以外を3秒映すと自動待機）"));
  saveSettings();
  updateScores();
  syncDiscord(false);
}

void MainDock::updatePreview(const QImage &frame, const std::vector<Standing> &standings) {
  const auto activeTags = session_.tags().empty() ? configuredTags() : session_.tags();
  preview_->setPreview(frame, standings, activeTags, latest_.evidence, latest_.cumulativeScores);
}

void MainDock::receiveFrame(Frame frame) {
  if (!running_)
    return;
  const qint64 now = clock_.elapsed();
  lastFrameMs_ = now;
  struct PooledFrame {
    std::vector<uint8_t> rgba;
    std::shared_ptr<FrameBufferPool> pool;
    ~PooledFrame() {
      if (pool)
        pool->release(std::move(rgba));
    }
  };
  auto *pooled = new PooledFrame{std::move(frame.rgba), capture_ ? capture_->bufferPool() : nullptr};
  latestImage_ = QImage(
      pooled->rgba.data(), frame.width, frame.height, frame.stride, QImage::Format_RGBA8888,
      [](void *p) { delete static_cast<PooledFrame *>(p); }, pooled);
  const bool holdCommittedPreview =
      gate_.locked() && !lastCommittedFrame_.isNull() && lastCommittedStandings_.size() == 12 &&
      (latest_.complete() || latest_.layout.visible || latest_.isCumulativeStandings());
  if (!reviewing_ && !holdCommittedPreview && preview_->isVisible() &&
      (lastPreviewMs_ == 0 || (now - lastPreviewMs_) >= 1000)) {
    lastPreviewMs_ = now;
    updatePreview(latestImage_);
  }
  if (busy_ || !analyzer_)
    return;
  {
    std::lock_guard<std::mutex> lock(workerMutex_);
    jobImage_ = latestImage_;
    jobFrameReceivedAt_ = QDateTime::currentDateTimeUtc();
    jobTags_ = session_.tags();
    jobGeneration_ = generation_;
    jobReady_ = true;
    busy_ = true;
    workerBusy_.store(true, std::memory_order_release);
  }
  workerCv_.notify_one();
}

void MainDock::workerLoop() {
#ifdef _WIN32
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
  CourseRecognizer courseRec;
  for (;;) {
    QImage image;
    std::vector<std::string> tags;
    uint64_t generation;
    QDateTime frameReceivedAt;
    {
      std::unique_lock<std::mutex> lock(workerMutex_);
      workerCv_.wait(lock, [this] { return workerStop_ || jobReady_; });
      if (workerStop_)
        return;
      image = std::move(jobImage_);
      tags = jobTags_;
      generation = jobGeneration_;
      frameReceivedAt = jobFrameReceivedAt_;
      jobReady_ = false;
    }
    Analysis result;
    QElapsedTimer elapsed;
    elapsed.start();
    try {
      QString courseName;
      if (CourseRecognizer::isCourseScreen(image)) {
        if (auto c = courseRec.recognize(image))
          courseName = QString::fromStdString(*c);
        result.standings.reserve(12);
        result.evidence.resize(12);
        for (int r = 0; r < 12; ++r)
          result.standings.push_back({r + 1, {}, 0});
      } else {
        result = analyzer_->read(image, tags, {}, false, true);
      }
      result.detectedCourse = std::move(courseName);
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
  workerBusy_.store(false, std::memory_order_release);
  if (!running_ || generation != generation_)
    return;
  lastAnalysisMs_ = clock_.elapsed();
  latest_ = std::move(result);
  latestFrameReceivedAt_ = frameReceivedAt;
  const bool highFreqNeeded = !latest_.detectedCourse.isEmpty() || latest_.layout.anchors >= 3 ||
                              latest_.layout.visible || standingsStableCount_ > 0;
  capture_->setIntervalSeconds(highFreqNeeded ? 0.100f : 0.250f);
  if (!latest_.error.isEmpty()) {
    const auto error = latest_.error;
    stopAggregation();
    setStatus(error, "#ff8a80");
    return;
  }
  const std::optional<std::string> courseObs =
      latest_.detectedCourse.isEmpty()
          ? std::nullopt
          : std::optional<std::string>(latest_.detectedCourse.toStdString());
  if (auto lockedCourse =
          courseGate_.observe(courseObs, uint64_t(clock_.elapsed()), true)) {
    const QString course = QString::fromStdString(*lockedCourse);
    currentCourse_ = course;
    updateCourseDisplay();
    updateScores();
    blog(LOG_INFO, "[MK8DX QuickScore] detected course: %s", course.toUtf8().constData());
    if (postDiscord_->isChecked() && course != lastSentCourse_)
      syncDiscordCourse(course);
  }
  diagnostic_->setText(
      tr("%1×%2 ｜ 順位%3/12 ｜ 加点%4/12 ｜ %5ms")
          .arg(latestImage_.width())
          .arg(latestImage_.height())
          .arg(latest_.layout.anchors)
          .arg(latest_.layout.pluses)
          .arg(duration));

  if (reviewing_ && editMode_ == EditMode::CumulativeReconcile && !userTouchedReview_ &&
      latest_.complete()) {
    cancelReview();
  }
  const bool ready = latest_.complete();
  if (ready) {
    courseGate_.reset();
    lastSentCourse_.clear();
  }
  const bool immediate = ready && latest_.finished_in_twelfth;
  const bool stable = gate_.observe(latest_.layout.visible, ready, ready ? "decision" : "",
                                    uint64_t(clock_.elapsed()), immediate);
  if (requestCycle_ != gate_.cycle()) {
    standingsRequestActive_ = false;
    requestIssued_ = false;
    requestCycle_ = gate_.cycle();
  }
  if (reviewing_) {
    const bool editingPast =
        (editMode_ == EditMode::ExistingRace || editMode_ == EditMode::PendingFirstRace) &&
        !retryInFlight_;
    if (editingPast && stable && !requestIssued_ && !api_->busy() && !userTouchedReview_) {
      cancelReview();
    } else if (editingPast && (clock_.elapsed() - reviewOpenedMs_) >= 60000) {
      cancelReview();
      if (!latestImage_.isNull())
        updatePreview(latestImage_);
      setStatus(tr("編集画面を1分間操作しなかったため、ライブ監視へ戻りました。"));
      return;
    } else {
      return;
    }
  }
  const bool readerBusy = api_->busy();
  if (readerBusy) {
    setStatus(tr("リザルトを判定しています。停止で中断できます。"));
  } else if (stable && !requestIssued_) {
    standingsStableCount_ = 0;
    standingsLastTotal_ = -1;
    requestRecognition(latest_);
  } else if (!reviewing_ && !latest_.complete() && latest_.isCumulativeStandings()) {
    const int impliedRaces = latest_.cumulativeScores.impliedRaceCount();
    const int totalPts = latest_.cumulativeScores.totalPoints;
    const int effRaces = int(session_.effectiveRaceCount());
    const bool hasOffset = session_.inGameScoreOffset() > 0;
    const int expectedOffsetTotal = 82 * effRaces - session_.inGameScoreOffset();
    const bool needForPending1R =
        !hasOffset && session_.hasPendingFirstRace() && impliedRaces == effRaces;
    const bool needForLateStartFallback =
        !hasOffset && !session_.hasPendingFirstRace() && session_.races().size() == 1 &&
        session_.races()[0].raceSpan == 1 && !session_.races()[0].corrected &&
        impliedRaces >= 2 && verifiedRaceCount_ < impliedRaces;
    const bool needForAudit = !hasOffset && !session_.hasPendingFirstRace() &&
                              !session_.races().empty() && impliedRaces == effRaces &&
                              verifiedRaceCount_ < impliedRaces;
    if (hasOffset && !session_.hasPendingFirstRace() && !session_.races().empty() &&
        totalPts == expectedOffsetTotal && verifiedRaceCount_ < effRaces) {
      verifiedRaceCount_ = effRaces;
      setStatus(tr("第%1レース 総合得点一致（回線落ち・部屋落ち補正済み: ゲーム内 %2点 / 累計 %3点）")
                    .arg(effRaces)
                    .arg(totalPts)
                    .arg(82 * effRaces));
    } else if ((needForPending1R || needForLateStartFallback || needForAudit) &&
               (standingsCheckedRace_ != impliedRaces || standingsCheckedTotal_ != totalPts)) {
      std::array<int, 12> currentRows{};
      for (int r = 0; r < 12; ++r)
        currentRows[size_t(r)] = latest_.cumulativeScores.rows[size_t(r)].points;
      if (standingsLastTotal_ == totalPts && standingsStableRows_ == currentRows) {
        ++standingsStableCount_;
      } else {
        standingsLastTotal_ = totalPts;
        standingsStableRows_ = currentRows;
        standingsStableCount_ = 1;
        standingsFirstMs_ = clock_.elapsed();
      }
      if (standingsStableCount_ >= 2 && clock_.elapsed() - standingsFirstMs_ >= 120) {
        standingsStableCount_ = 0;
        requestStandingsVerification(latest_);
      } else {
        setStatus(tr("総合順位表（第%1レース）を検算待機中。").arg(impliedRaces));
      }
    }
  } else if (gate_.locked()) {
    standingsStableCount_ = 0;
    setStatus(tr("このリザルトは処理済みです。次のレースを待機中。"));
  } else if (!gate_.armed()) {
    standingsStableCount_ = 0;
    setStatus(tr("開始準備：リザルト以外を3秒映すか「現在のリザルトを確認」を押してください。"));
  } else if (!latest_.layout.visible) {
    standingsStableCount_ = 0;
    setStatus(tr("リザルト待機中。"));
  } else {
    setStatus(tr("リザルトの安定を待機中（12順位・加点記号を確認しています）。"));
  }
}

void MainDock::requestRetryRecognition() {
  if (!reviewing_ || pending_.image.isNull() || api_->busy())
    return;
  const bool offlineEdit =
      (editMode_ == EditMode::ExistingRace || editMode_ == EditMode::PendingFirstRace);
  if (!running_ && !offlineEdit)
    return;
  const auto configurationError = api_->configurationError();
  if (!configurationError.isEmpty()) {
    setStatus(configurationError, "#ffb74d");
    return;
  }
  const auto generation = generation_;
  const auto captured = pending_;
  const auto mode = editMode_;
  const int editIdx = editingRaceIndex_;
  retryInFlight_ = true;
  retryRead_->setEnabled(false);
  confirm_->setEnabled(false);
  preview_->setEnabled(false);
  setStatus(tr("リザルトを再判定しています。"));
  const auto manualSelections = currentEditorStandings();
  api_->read(captured, session_.tags(),
             [this, generation, mode, editIdx, manualSelections](Analysis result) {
    retryInFlight_ = false;
    if (!reviewing_ || generation != generation_)
      return;
    applyPlayerOverrides(result, playerOverrides_, session_.tags());
    for (int row = 0; row < 12 && size_t(row) < result.standings.size(); ++row) {
      if (size_t(row) < manualSelections.size() && !manualSelections[size_t(row)].tag.empty()) {
        result.standings[size_t(row)].tag = manualSelections[size_t(row)].tag;
        result.standings[size_t(row)].confidence = 1;
      }
    }
    enforceTwelfthFinishNotice(result, session_.tags());
    if (session_.hasAutoConfirmedRace())
      resolveSingleDeficitRow(result, session_.tags(), session_.playersPerTeam());
    api_->cancel();
    confirm_->setEnabled(true);
    preview_->setEnabled(true);
    retryRead_->setEnabled(true);
    result.cumulativeScores = pending_.cumulativeScores;
    review(result, mode, editIdx);
    setStatus(result.error.isEmpty()
                  ? tr("再判定が完了しました。右側バッジを確認して確定してください。")
                  : result.error + tr(" 右側のバッジからチームを選択できます。"),
              "#ffb74d");
  });
}

void MainDock::requestRecognition(const Analysis &captured) {
  if (!running_ || reviewing_ || api_->busy() || requestIssued_ || !captured.complete())
    return;
  requestIssued_ = true;
  requestCycle_ = gate_.cycle();
  gate_.lock();
  const auto generation = generation_;
  const auto requestedAt = clock_.elapsed();
  QJsonArray tags;
  for (const auto &tag : session_.tags())
    tags.append(QString::fromStdString(tag));
  const auto capture = saveResultCapture(
      QDir(configDirectory_).absoluteFilePath("captures"), captured.image,
      QJsonObject{{"version", QUICKSCORE_VERSION},
                  {"source", source_->currentText()},
                  {"frameReceivedAt", latestFrameReceivedAt_.toString(Qt::ISODateWithMs)},
                  {"format", formatNameForPlayers(session_.playersPerTeam())},
                  {"tags", tags},
                  {"anchors", captured.layout.anchors},
                  {"pluses", captured.layout.pluses}});
  resultCapture_ = capture.files;
  if (capture.error.isEmpty()) {
    blog(LOG_INFO, "[MK8DX QuickScore] saved result frame in %lld ms: %s",
         static_cast<long long>(capture.durationMs),
         capture.files["fullFrame"].toString().toUtf8().constData());
  } else {
    resultCapture_.insert("error", capture.error);
    blog(LOG_WARNING, "[MK8DX QuickScore] result image save failed: %s",
         capture.error.toUtf8().constData());
  }

  auto completion = [this, generation, requestedAt, captured](Analysis result) {
    if (!running_ || generation != generation_)
      return;
    applyPlayerOverrides(result, playerOverrides_, session_.tags());
    enforceTwelfthFinishNotice(result, session_.tags());
    if (session_.hasAutoConfirmedRace())
      resolveSingleDeficitRow(result, session_.tags(), session_.playersPerTeam());
    resultCapture_.insert("apiModel", api_->lastModel);
    resultCapture_.insert("apiUsage", api_->lastUsage);
    latest_ = std::move(result);
    const auto recognized =
        std::count_if(latest_.standings.begin(), latest_.standings.end(),
                      [](const Standing &standing) { return !standing.tag.empty(); });
    diagnostic_->setText(tr("判定 12/12 ｜ 一致 %1/12 ｜ %2ms")
                             .arg(recognized)
                             .arg(clock_.elapsed() - requestedAt));
    const QString error = latest_.error;
    const auto validation = session_.validate(latest_.standings);
    if (error.isEmpty() && validation.ok) {
      QString eventNote;
      if (commit(latest_.standings, false, latest_, &eventNote)) {
        if (!checkAutoFinish(false))
          setStatus(eventNote.isEmpty() ? tr("12順位・チーム人数を検証し自動集計しました。")
                                        : eventNote);
        return;
      }
      const auto storageError = status_->text();
      review(latest_);
      setStatus(storageError + tr(" 保存先を確認し「確認した内容で集計」で再試行できます。"),
                "#ff8a80");
      return;
    }
    const StandingsScoreRead preScores = readCumulativeScores(captured.image);
    const bool looksLikeRace1 = (preScores.validRows < 12 || preScores.totalPoints == 0);
    if (looksLikeRace1 && session_.races().empty() && !session_.hasPendingFirstRace()) {
      Session proposed = session_;
      proposed.deferFirstRace(latest_.standings, currentCourse_.toStdString());
      pendingFirstAudit_ = QJsonObject{
          {"race", 1},
          {"capturedAt", resultCapture_["frameReceivedAt"].toString(
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))},
          {"capture", resultCapture_},
          {"raw", evidenceJson(latest_)}};
      QString saveError;
      if (storeSession(configDirectory_ + "/session.json", proposed, audit_, saveError)) {
        session_ = std::move(proposed);
        clearCurrentCourse();
        lastCommittedFrame_ = captured.image;
        lastCommittedStandings_ = latest_.standings;
        updateScores();
        refreshRaceHistory();
        updatePreview(captured.image, latest_.standings);
        if (postDiscord_->isChecked())
          syncDiscord(true);
        setStatus(tr("1レース目で未一致・エラーがあったため保留しました（名前修正待ち）。2レース"
                     "目以降を継続し、名前が直り次第 総合順位表から自動回収します。"),
                  "#ffb74d");
        return;
      }
    }
    review(latest_);
    if (!error.isEmpty())
      setStatus(error + tr(" 下の12人のチームを選び「確認した内容で集計」を押してください。"),
                "#ffb74d");
    else
      setStatus(tr("読み取れない文字またはチーム人数の不一致があります。下の結果を確認してくだ"
                   "さい。"),
                "#ffb74d");
  };

  setStatus(tr("12人のチームを判定中です。停止で中断できます。"));
  api_->read(captured, session_.tags(), std::move(completion));
}

void MainDock::requestStandingsVerification(const Analysis &captured) {
  if (!running_ || reviewing_ || api_->busy() || !captured.isCumulativeStandings())
    return;
  const int impliedRaces = captured.cumulativeScores.impliedRaceCount();
  const int totalPts = captured.cumulativeScores.totalPoints;
  standingsCheckedRace_ = impliedRaces;
  standingsCheckedTotal_ = totalPts;
  standingsRequestActive_ = true;
  const auto generation = generation_;

  auto completion = [this, generation, captured, impliedRaces](Analysis result) {
    standingsRequestActive_ = false;
    if (!running_ || reviewing_ || generation != generation_)
      return;
    result.cumulativeScores = captured.cumulativeScores;
    applyPlayerOverrides(result, playerOverrides_, session_.tags());
    const auto validation = session_.validate(result.standings);
    if (!result.error.isEmpty() || !validation.ok) {
      review(result, EditMode::CumulativeReconcile, impliedRaces);
      setStatus(tr("総合順位表の検算で曖昧な行があります。確認して反映するかスキップしてくださ"
                   "い（次のレースで自動待機します）。"),
                "#ffb74d");
      return;
    }

    std::map<std::string, int> gameTeamTotal;
    for (const auto &t : session_.tags())
      gameTeamTotal[t] = 0;
    for (int row = 0; row < 12; ++row)
      gameTeamTotal[result.standings[size_t(row)].tag] +=
          captured.cumulativeScores.rows[size_t(row)].points;

    Session proposed = session_;
    bool changed = false;
    if (!proposed.reconcileWithCumulativeTotals(gameTeamTotal, impliedRaces, &changed))
      return;

    auto audit = audit_;
    const bool prependedRace = proposed.races().size() > session_.races().size();
    const bool recoveredPending1R = prependedRace && session_.hasPendingFirstRace();
    if (prependedRace) {
      QJsonObject entry = recoveredPending1R
                              ? pendingFirstAudit_
                              : QJsonObject{{"raceSpan", proposed.races().front().raceSpan},
                                            {"label", QString::fromStdString(
                                                          proposed.races().front().label)}};
      if (entry.isEmpty())
        entry = QJsonObject{{"race", proposed.races().front().number}};
      entry.insert("corrected", true);
      entry.insert("reconciledAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
      audit.prepend(entry);
      int nextAuditRace = 1;
      for (int i = 0; i < audit.size(); ++i) {
        auto obj = audit[i].toObject();
        const int span = std::max(1, obj.value("raceSpan").toInt(1));
        nextAuditRace += span - 1;
        obj.insert("race", nextAuditRace);
        ++nextAuditRace;
        audit[i] = obj;
      }
    }

    QString error;
    if (!storeSession(configDirectory_ + "/session.json", proposed, audit, error)) {
      setStatus(tr("検算結果の保存に失敗しました: %1").arg(error), "#ff8a80");
      return;
    }
    session_ = std::move(proposed);
    audit_ = audit;
    if (recoveredPending1R)
      pendingFirstAudit_ = {};
    verifiedRaceCount_ = impliedRaces;
    lastCommittedFrame_ = captured.image;
    lastCommittedStandings_ = result.standings;
    updateScores();
    refreshRaceHistory();
    updatePreview(captured.image, result.standings);
    const bool autoFinished = checkAutoFinish(false);
    if ((changed || prependedRace || autoFinished) && postDiscord_->isChecked())
      syncDiscord(changed || prependedRace);
    if (!autoFinished) {
      if (recoveredPending1R)
        setStatus(
            tr("総合順位表から1レース目を逆算・自動回収し、第%1レースまで同期しました。")
                .arg(impliedRaces));
      else if (prependedRace)
        setStatus(tr("総合順位表から途中開始前の合計点（%1）を補完し、第%2レースまで同期しました。")
                      .arg(QString::fromStdString(session_.races().front().label))
                      .arg(impliedRaces));
      else if (changed)
        setStatus(tr("総合順位表で検算し、序盤のズレを自動補正しました（第%1レース時点）。")
                      .arg(impliedRaces));
      else
        setStatus(tr("第%1レース 総合順位表の検算OK（ゲーム内合計と完全一致）。").arg(impliedRaces));
    }
  };

  setStatus(tr("総合順位表（第%1レース）を検算中です。").arg(impliedRaces));
  api_->read(captured, session_.tags(), std::move(completion));
}

void MainDock::review(const Analysis &analysis, EditMode mode, int raceIndex) {
  if (analysis.standings.size() != 12)
    return;
  if (mode == EditMode::NewRace || mode == EditMode::CumulativeReconcile)
    ++generation_;
  pending_ = analysis;
  reviewing_ = true;
  editMode_ = mode;
  editingRaceIndex_ = raceIndex;
  userTouchedReview_ = false;
  retryInFlight_ = false;
  reviewOpenedMs_ = clock_.elapsed();
  if (raceSelector_)
    raceSelector_->setEnabled(false);

  const auto activeTags = session_.tags().empty() ? configuredTags() : session_.tags();
  reviewBaselineStandings_ = pending_.standings;
  preview_->setPreview(pending_.image, pending_.standings, activeTags, pending_.evidence,
                       pending_.cumulativeScores);
  preview_->setEditing(true, mode == EditMode::CumulativeReconcile);

  if (mode == EditMode::ExistingRace || mode == EditMode::PendingFirstRace) {
    confirm_->setText(tr("修正を保存"));
    cancel_->setText(tr("やめる"));
  } else if (mode == EditMode::CumulativeReconcile) {
    confirm_->setText(tr("総合で検算"));
    cancel_->setText(tr("スキップ"));
    setStatus(tr("総合順位表（%1R）の右バッジを確認して検算してください。").arg(raceIndex),
              "#ffb74d");
  } else {
    confirm_->setText(tr("この内容で集計する"));
    cancel_->setText(tr("保留して次へ"));
  }
  confirm_->show();
  if (reviewSubRow_)
    reviewSubRow_->show();
  cancel_->show();
  retryRead_->show();
  if (reviewCourseRow_ && reviewCourse_) {
    const bool showCourseEdit = (mode != EditMode::CumulativeReconcile);
    reviewCourseRow_->setVisible(showCourseEdit);
    if (showCourseEdit) {
      QString initialCourse;
      if (mode == EditMode::ExistingRace && raceIndex >= 0 &&
          size_t(raceIndex) < session_.races().size()) {
        initialCourse = QString::fromStdString(session_.races()[size_t(raceIndex)].course);
      } else if (mode == EditMode::PendingFirstRace) {
        initialCourse = QString::fromStdString(session_.pendingFirstRaceCourse());
      } else {
        initialCourse = !pending_.detectedCourse.isEmpty() ? pending_.detectedCourse : currentCourse_;
      }
      const QSignalBlocker blocker(reviewCourse_);
      reviewCourse_->setText(initialCourse);
    }
  }
  reviewHelp_->hide();
  if (mode == EditMode::ExistingRace || mode == EditMode::PendingFirstRace) {
    retryRead_->setVisible(!pending_.image.isNull() && api_->configurationError().isEmpty());
  }
  if (mode == EditMode::NewRace)
    setStatus(tr("右バッジでチーム確認（次レース以降に自動学習）"), "#ffb74d");
  else if (mode == EditMode::ExistingRace || mode == EditMode::PendingFirstRace)
    setStatus(tr("右バッジ変更 →「修正を保存」（自動学習あり）"));
}

std::vector<Standing> MainDock::currentEditorStandings() const {
  auto out = preview_->standings();
  if (out.size() != 12) {
    out.clear();
    out.reserve(12);
    for (int row = 0; row < 12; ++row)
      out.push_back({row + 1, preview_->rowTag(row).toStdString(), 1.0});
  }
  return out;
}

void MainDock::learnOverridesFromReview(const std::vector<Standing> &confirmed) {
  if (pending_.image.isNull() || confirmed.size() != 12)
    return;
  bool updated = false;
  for (int row = 0; row < 12; ++row) {
    const std::string &tag = confirmed[size_t(row)].tag;
    if (tag.empty())
      continue;
    if (size_t(row) < pending_.evidence.size() &&
        (pending_.evidence[size_t(row)].method == QStringLiteral("欠員・CPU自動補完") ||
         pending_.evidence[size_t(row)].method == QStringLiteral("自チーム12位確定通知")))
      continue;
    const std::string prevTag =
        size_t(row) < reviewBaselineStandings_.size() ? reviewBaselineStandings_[size_t(row)].tag
                                                      : std::string{};
    if (tag == prevTag)
      continue;
    const QImage patch = extractPlayerNamePatch(pending_.image, row);
    if (patch.isNull())
      continue;
    bool replacedExisting = false;
    for (auto &ov : playerOverrides_) {
      if (matchPlayerNameEdge(pending_.image, row, ov.patch) >= 0.85) {
        ov.tag = tag;
        ov.patch = patch;
        replacedExisting = true;
        updated = true;
        break;
      }
    }
    if (!replacedExisting) {
      if (playerOverrides_.size() >= 36)
        playerOverrides_.erase(playerOverrides_.begin());
      playerOverrides_.push_back(TagSample{tag, patch});
      updated = true;
    }
  }
  if (updated && !configDirectory_.isEmpty()) {
    QString err;
    if (!saveSamples(configDirectory_ + "/player-overrides.json", playerOverrides_, err)) {
      blog(LOG_WARNING, "[MK8DX QuickScore] failed to save player overrides: %s",
           err.toUtf8().constData());
    }
  }
}

void MainDock::refreshRaceHistory() {
  if (!raceSelector_)
    return;
  const QSignalBlocker blocker(raceSelector_);
  raceSelector_->clear();
  const auto &races = session_.races();
  const bool hasAny = session_.hasPendingFirstRace() || !races.empty();
  if (!hasAny) {
    raceSelector_->addItem(tr("確定レースなし"), -99);
    raceSelector_->setEnabled(false);
    return;
  }
  raceSelector_->addItem(
      tr("過去のレースを修正... (全%1R)").arg(int(session_.effectiveRaceCount())), -99);
  if (session_.hasPendingFirstRace()) {
    QString pendingText = tr("1R");
    if (!session_.pendingFirstRaceCourse().empty())
      pendingText += tr(" - %1").arg(QString::fromStdString(session_.pendingFirstRaceCourse()));
    pendingText += tr(" (未確定・名前待ち)");
    raceSelector_->addItem(pendingText, -1);
  }
  for (size_t i = 0; i < races.size(); ++i) {
    QString text = races[i].label.empty() ? tr("%1R").arg(races[i].number)
                                          : QString::fromStdString(races[i].label);
    if (!races[i].course.empty())
      text += tr(" - %1").arg(QString::fromStdString(races[i].course));
    if (races[i].corrected && races[i].raceSpan <= 1)
      text += tr(" (補正済み)");
    raceSelector_->addItem(text, int(i));
  }
  raceSelector_->setCurrentIndex(0);
  raceSelector_->setEnabled(!reviewing_);
}

void MainDock::openSelectedRaceEditor() {
  if (reviewing_ || !raceSelector_)
    return;
  const int data = raceSelector_->currentData().toInt();
  if (data == -99)
    return;
  Analysis loaded;
  if (data == -1 && session_.hasPendingFirstRace()) {
    loaded.standings = session_.pendingFirstRaceStandings();
    if (loaded.standings.size() != 12) {
      loaded.standings.clear();
      for (int r = 0; r < 12; ++r)
        loaded.standings.push_back({r + 1, {}, 0.0});
    }
    const QString fullPath =
        pendingFirstAudit_.value("capture").toObject().value("fullFrame").toString();
    if (!fullPath.isEmpty())
      loaded.image = QImage(fullPath);
    if (loaded.image.isNull() && !lastCommittedFrame_.isNull())
      loaded.image = lastCommittedFrame_;
    if (loaded.image.isNull()) {
      loaded.image = QImage(1920, 1080, QImage::Format_ARGB32);
      loaded.image.fill(QColor(24, 28, 34));
    }
    review(loaded, EditMode::PendingFirstRace, -1);
    return;
  }
  if (data < 0 || size_t(data) >= session_.races().size())
    return;
  const auto &race = session_.races()[size_t(data)];
  if (race.raceSpan > 1) {
    QMessageBox::information(this, tr("修正不可"),
                             tr("途中開始時に取り込んだ合計（%1）は個別の12人順位を持たないため編集対象外です。")
                                 .arg(QString::fromStdString(race.label)));
    return;
  }
  loaded.standings = race.standings;
  if (loaded.standings.size() != 12) {
    loaded.standings.clear();
    for (int r = 0; r < 12; ++r)
      loaded.standings.push_back({r + 1, {}, 0.0});
  }
  if (data < audit_.size()) {
    const QString fullPath =
        audit_[data].toObject().value("capture").toObject().value("fullFrame").toString();
    if (!fullPath.isEmpty())
      loaded.image = QImage(fullPath);
  }
  if (loaded.image.isNull()) {
    loaded.image = QImage(1920, 1080, QImage::Format_ARGB32);
    loaded.image.fill(QColor(24, 28, 34));
  }
  review(loaded, EditMode::ExistingRace, data);
}

void MainDock::confirmPending() {
  if (!reviewing_ || pending_.standings.size() != 12)
    return;
  auto standings = currentEditorStandings();
  const auto validation = session_.validate(standings);
  if (!validation.ok) {
    QStringList errors;
    for (const auto &error : validation.errors)
      errors << QString::fromStdString(error);
    QMessageBox::warning(this, tr("確認してください"), errors.join("\n"));
    return;
  }

  if (editMode_ == EditMode::ExistingRace) {
    const int editIdx = editingRaceIndex_;
    Session proposed = session_;
    if (editIdx < 0 || !proposed.replaceRace(size_t(editIdx), standings, true))
      return;
    const QString editedCourse = editedReviewCourse();
    proposed.setRaceCourse(size_t(editIdx), editedCourse.toStdString());
    const int raceNumber = proposed.races()[size_t(editIdx)].number;
    auto audit = audit_;
    if (editIdx < audit.size()) {
      auto obj = audit[editIdx].toObject();
      obj.insert("course", editedCourse);
      obj.insert("corrected", true);
      obj.insert("editedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
      audit[editIdx] = obj;
    }
    QString error;
    if (!storeSession(configDirectory_ + "/session.json", proposed, audit, error)) {
      setStatus(tr("保存できません: %1").arg(error), "#ff8a80");
      return;
    }
    session_ = std::move(proposed);
    audit_ = audit;
    const QImage img = pending_.image;
    learnOverridesFromReview(standings);
    cancelReview();
    lastCommittedFrame_ = img;
    lastCommittedStandings_ = standings;
    updateScores();
    refreshRaceHistory();
    updatePreview(img, standings);
    if (postDiscord_->isChecked())
      syncDiscord();
    setStatus(tr("第%1レースの修正を保存し再集計しました。").arg(raceNumber));
    return;
  }

  if (editMode_ == EditMode::PendingFirstRace) {
    Session proposed = session_;
    const QString editedCourse = editedReviewCourse();
    proposed.setPendingFirstRaceCourse(editedCourse.toStdString());
    if (!proposed.resolvePendingFirstRaceManually(standings, true))
      return;
    auto audit = audit_;
    QJsonObject entry = pendingFirstAudit_;
    if (entry.isEmpty())
      entry = QJsonObject{{"race", 1}};
    entry.insert("course", editedCourse);
    entry.insert("corrected", true);
    entry.insert("committedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    audit.prepend(entry);
    for (int i = 0; i < audit.size(); ++i) {
      auto obj = audit[i].toObject();
      obj.insert("race", i + 1);
      audit[i] = obj;
    }
    QString error;
    if (!storeSession(configDirectory_ + "/session.json", proposed, audit, error)) {
      setStatus(tr("保存できません: %1").arg(error), "#ff8a80");
      return;
    }
    session_ = std::move(proposed);
    audit_ = audit;
    pendingFirstAudit_ = {};
    const QImage img = pending_.image;
    learnOverridesFromReview(standings);
    cancelReview();
    lastCommittedFrame_ = img;
    lastCommittedStandings_ = standings;
    updateScores();
    refreshRaceHistory();
    updatePreview(img, standings);
    const bool autoFinished = checkAutoFinish(false);
    if (postDiscord_->isChecked())
      syncDiscord();
    if (!autoFinished)
      setStatus(tr("1レース目を確定し全体の点数を再集計しました。"));
    return;
  }

  if (editMode_ == EditMode::CumulativeReconcile) {
    const int impliedRaces = editingRaceIndex_;
    std::map<std::string, int> gameTeamTotal;
    for (const auto &t : session_.tags())
      gameTeamTotal[t] = 0;
    for (int row = 0; row < 12; ++row)
      gameTeamTotal[standings[size_t(row)].tag] +=
          pending_.cumulativeScores.rows[size_t(row)].points;

    Session proposed = session_;
    bool changed = false;
    if (!proposed.reconcileWithCumulativeTotals(gameTeamTotal, impliedRaces, &changed)) {
      setStatus(tr("総合得点との整合性が取れないため反映できませんでした。"), "#ff8a80");
      return;
    }
    auto audit = audit_;
    const bool prependedRace = proposed.races().size() > session_.races().size();
    const bool recoveredPending1R = prependedRace && session_.hasPendingFirstRace();
    if (prependedRace) {
      QJsonObject entry = recoveredPending1R
                              ? pendingFirstAudit_
                              : QJsonObject{{"raceSpan", proposed.races().front().raceSpan},
                                            {"label", QString::fromStdString(
                                                          proposed.races().front().label)}};
      if (entry.isEmpty())
        entry = QJsonObject{{"race", proposed.races().front().number}};
      entry.insert("corrected", true);
      entry.insert("reconciledAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
      audit.prepend(entry);
      int nextAuditRace = 1;
      for (int i = 0; i < audit.size(); ++i) {
        auto obj = audit[i].toObject();
        const int span = std::max(1, obj.value("raceSpan").toInt(1));
        nextAuditRace += span - 1;
        obj.insert("race", nextAuditRace);
        ++nextAuditRace;
        audit[i] = obj;
      }
    }
    QString error;
    if (!storeSession(configDirectory_ + "/session.json", proposed, audit, error)) {
      setStatus(tr("保存できません: %1").arg(error), "#ff8a80");
      return;
    }
    session_ = std::move(proposed);
    audit_ = audit;
    if (recoveredPending1R)
      pendingFirstAudit_ = {};
    verifiedRaceCount_ = impliedRaces;
    const QImage img = pending_.image;
    learnOverridesFromReview(standings);
    cancelReview();
    lastCommittedFrame_ = img;
    lastCommittedStandings_ = standings;
    updateScores();
    refreshRaceHistory();
    updatePreview(img, standings);
    const bool autoFinished = checkAutoFinish(false);
    if ((changed || prependedRace || autoFinished) && postDiscord_->isChecked())
      syncDiscord(changed || prependedRace);
    if (!autoFinished)
      setStatus(tr("総合順位表（第%1レース）の検算結果を反映しました。").arg(impliedRaces));
    return;
  }

  QString eventNote;
  const std::optional<QString> courseOverride =
      (reviewCourseRow_ && !reviewCourseRow_->isHidden())
          ? std::optional<QString>(editedReviewCourse())
          : std::nullopt;
  if (!commit(standings, true, pending_, &eventNote, courseOverride))
    return;
  learnOverridesFromReview(standings);
  gate_.lock();
  cancelReview();
  if (!checkAutoFinish(false))
    setStatus(eventNote.isEmpty() ? tr("確認済みの結果を集計しました。") : eventNote);
}

void MainDock::cancelReview() {
  if (retryInFlight_) {
    ++generation_;
    if (api_->busy())
      api_->cancel();
  }
  retryInFlight_ = false;
  reviewing_ = false;
  editMode_ = EditMode::None;
  editingRaceIndex_ = -1;
  userTouchedReview_ = false;
  reviewOpenedMs_ = 0;
  pending_ = {};
  reviewBaselineStandings_.clear();
  preview_->setEditing(false);
  preview_->setEnabled(true);
  confirm_->setEnabled(true);
  retryRead_->setEnabled(true);
  confirm_->setText(tr("この内容で集計する"));
  cancel_->setText(tr("保留して次へ"));
  reviewHelp_->setText(tr("右のバッジをクリックしてチームを選択してください。"));
  for (auto *w : std::initializer_list<QWidget *>{reviewHelp_, reviewCourseRow_, confirm_,
                                                 reviewSubRow_, cancel_, retryRead_})
    if (w)
      w->hide();
  refreshRaceHistory();
}

bool MainDock::commit(const std::vector<Standing> &standings, bool corrected, const Analysis &raw,
                      QString *eventNote, std::optional<QString> courseOverride) {
  Session proposed = session_;
  auto audit = audit_;
  QString note;

  if (!raw.image.isNull() && standings.size() == 12) {
    const StandingsScoreRead preScores = readCumulativeScores(raw.image);
    if (preScores.validRows == 12) {
      const int effBefore = int(proposed.effectiveRaceCount());
      if (proposed.races().empty() && !proposed.hasPendingFirstRace()) {
        if (preScores.totalPoints > 0 && (preScores.totalPoints % 82) == 0) {
          const int priorRaces = preScores.totalPoints / 82;
          std::map<std::string, int> priorTotals;
          for (const auto &t : proposed.tags())
            priorTotals[t] = 0;
          for (size_t r = 0; r < 12; ++r)
            priorTotals[standings[r].tag] += preScores.rows[r].points;
          if (proposed.importPriorSummary(priorRaces, priorTotals)) {
            const auto &summary = proposed.races().front();
            audit.append(QJsonObject{
                {"race", summary.number},
                {"raceSpan", summary.raceSpan},
                {"label", QString::fromStdString(summary.label)},
                {"corrected", true},
                {"importedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
            note = tr("途中開始（%1）の総合点を自動取り込みし、第%2レースを集計しました。")
                       .arg(QString::fromStdString(summary.label))
                       .arg(priorRaces + 1);
          }
        }
      } else if (effBefore >= 1) {
        const int expectedPreSum = 82 * effBefore - proposed.inGameScoreOffset();
        if (preScores.totalPoints == 0) {
          proposed.setInGameScoreOffset(82 * effBefore);
          note = tr("部屋落ち（全員0点リセット）を検出し、累計点を維持したまま集計しました。");
        } else if (preScores.totalPoints < expectedPreSum) {
          proposed.addInGameScoreOffset(expectedPreSum - preScores.totalPoints);
          note = tr("回線落ち・復帰による0点リセットを検出し、累計点を維持して集計しました。");
        }
      }
    }
  }

  const QString raceCourse = courseOverride.has_value()
                                 ? *courseOverride
                                 : (!raw.detectedCourse.isEmpty() ? raw.detectedCourse : currentCourse_);
  if (!proposed.commit(standings, corrected, nullptr, raceCourse.toStdString()))
    return false;
  audit.append(
      QJsonObject{{"race", int(proposed.effectiveRaceCount())},
                  {"course", raceCourse},
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
  if (eventNote)
    *eventNote = note;
  clearCurrentCourse();
  lastCommittedFrame_ = raw.image;
  lastCommittedStandings_ = standings;
  updateScores();
  refreshRaceHistory();
  updatePreview(raw.image, standings);
  if (running_ && session_.effectiveRaceCount() >= 12) {
    loungeOverlayActive_ = true;
    loungeOverlayUntilUtc_ = QDateTime::currentDateTimeUtc().addSecs(60);
  }
  if (postDiscord_->isChecked())
    syncDiscord();
  return true;
}

void MainDock::resetSession() {
  if (QMessageBox::question(this, tr("集計リセット"),
                            tr("現在の集計をバックアップしてリセットしますか？")) !=
      QMessageBox::Yes)
    return;
  if (running_)
    stopAggregation(false, false);
  const QString path = configDirectory_ + "/session.json";
  if (QFile::exists(path) &&
      !QFile::copy(path, path + "." +
                             QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") +
                             ".bak")) {
    setStatus(tr("バックアップできなかったため中止しました。"), "#ff8a80");
    return;
  }
  Session empty;
  const int resetPlayers = configuredPlayersPerTeam();
  empty.configure(configuredTags(), resetPlayers > 0 ? resetPlayers : 3);
  QString error;
  if (!storeSession(path, empty, {}, error)) {
    setStatus(error, "#ff8a80");
    return;
  }
  session_ = std::move(empty);
  audit_ = {};
  pendingFirstAudit_ = {};
  playerOverrides_.clear();
  if (!configDirectory_.isEmpty())
    QFile::remove(configDirectory_ + "/player-overrides.json");
  verifiedRaceCount_ = 0;
  standingsCheckedRace_ = -1;
  standingsCheckedTotal_ = -1;
  heldRaces_.clear();
  clearCurrentCourse();
  lastCommittedFrame_ = {};
  lastCommittedStandings_.clear();
  updateScores();
  refreshRaceHistory();
  loungeOverlayActive_ = false;
  loungeOverlayUntilUtc_ = {};
  if (postDiscord_->isChecked())
    syncDiscord(true, true);
  setStatus(tr("集計をリセットしました。次のレースを待機します。"));
}

void MainDock::clearCurrentCourse() {
  currentCourse_.clear();
  lastSentCourse_.clear();
  courseGate_.reset();
  updateCourseDisplay();
}

QString MainDock::editedReviewCourse() const {
  if (!reviewCourse_)
    return {};
  const QString raw = reviewCourse_->text().trimmed();
  if (raw.isEmpty() || raw == tr("未設定"))
    return {};
  if (const auto resolved = CourseRecognizer::resolveCourseInput(raw.toStdString()))
    return QString::fromStdString(*resolved);
  return raw;
}

void MainDock::updateCourseDisplay() {
  if (!courseLabel_)
    return;
  if (!currentCourse_.isEmpty()) {
    courseLabel_->setText(tr("現在コース：%1").arg(currentCourse_));
  } else if (!japaneseOcrAvailable_) {
    courseLabel_->setText(tr("現在コース：—（日本語OCR未導入）"));
  } else {
    courseLabel_->setText(tr("現在コース：—"));
  }
}

void MainDock::updateScores() {
  const auto totals = session_.totals();
  const auto activeTags = session_.tags().empty() ? configuredTags() : session_.tags();
  const QString scoreLine = formatScoreLine(activeTags, totals, session_.effectiveRaceCount(), 12);
  scores_->setText(scoreLine);

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

  const OverlayState overlay = buildOverlayState(
      activeTags, totals, session_.effectiveRaceCount(), 12, ++overlayRevision_);
  const QImage image = renderOverlayImage(overlay);
  std::vector<uint8_t> pixels(image.constBits(),
                              image.constBits() + size_t(image.bytesPerLine() * image.height()));
  updateScoreboardImage(image.width(), image.height(), std::move(pixels));

  const QByteArray stateJson =
      QJsonDocument(overlayStateToJson(overlay)).toJson(QJsonDocument::Compact);
  const QByteArray stateJs = overlayStateToJs(overlay);
  if (overlayServer_)
    overlayServer_->setState(stateJson, stateJs);

  auto writeOverlayFile = [&](const QString &name, const QByteArray &bytes) {
    if (configDirectory_.isEmpty())
      return;
    QFile file(configDirectory_ + "/" + name);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
      file.write(bytes);
  };
  writeOverlayFile(QStringLiteral("overlay-state.json"), stateJson);
  writeOverlayFile(QStringLiteral("overlay-state.js"), stateJs);
}

QJsonObject MainDock::buildLoungePayload(bool postToDiscord, bool resetMatch) const {
  const auto activeTags = session_.tags().empty() ? configuredTags() : session_.tags();
  const int players =
      session_.tags().empty() ? configuredPlayersPerTeam() : session_.playersPerTeam();
  const auto totalsMap = session_.totals();

  QJsonArray teamsJson;
  QJsonObject totalsJson;
  for (const auto &tag : activeTags) {
    const auto qtag = QString::fromStdString(tag);
    teamsJson.append(qtag);
    const auto it = totalsMap.find(tag);
    totalsJson.insert(qtag, it == totalsMap.end() ? 0 : it->second);
  }

  QJsonArray racesJson;
  for (const auto &race : session_.races()) {
    QJsonObject pointsJson;
    for (const auto &entry : race.points)
      pointsJson.insert(QString::fromStdString(entry.first), entry.second);
    QJsonArray standingsJson;
    for (const auto &s : race.standings) {
      standingsJson.append(
          QJsonObject{{"rank", s.rank}, {"tag", QString::fromStdString(s.tag)}});
    }
    QJsonObject raceObj{{"number", race.number},
                        {"race_span", std::max(1, race.raceSpan)},
                        {"course", QString::fromStdString(race.course)},
                        {"points", pointsJson},
                        {"standings", standingsJson}};
    if (!race.label.empty())
      raceObj.insert("label", QString::fromStdString(race.label));
    racesJson.append(raceObj);
  }

  QJsonArray heldJson;
  for (const auto &held : heldRaces_) {
    heldJson.append(QJsonObject{{"number", held.first}, {"course", held.second}});
  }

  QString channelId = QString::fromUtf8(QUICKSCORE_EMBEDDED_BOT_CHANNEL_ID).trimmed();
  if (channelId.isEmpty())
    channelId = QStringLiteral("1571454531814952971");

  QJsonObject obj{{"channel_id", channelId},
                  {"format", formatNameForPlayers(players)},
                  {"players_per_team", players},
                  {"teams", teamsJson},
                  {"totals", totalsJson},
                  {"races", racesJson},
                  {"current_course", currentCourse_},
                  {"race_count", int(session_.effectiveRaceCount())},
                  {"total_races", 12},
                  {"score_line", scores_->text()},
                  {"pending_first_race", session_.hasPendingFirstRace()},
                  {"pending_first_course",
                   QString::fromStdString(session_.pendingFirstRaceCourse())},
                  {"held_races", heldJson},
                  {"active", loungeOverlayActive_},
                  {"post_to_discord", postToDiscord}};
  const QString userId = configuredDiscordUserId();
  if (!userId.isEmpty())
    obj.insert("user_id", userId);
  if (resetMatch)
    obj.insert("event", QStringLiteral("reset"));
  if (loungeOverlayUntilUtc_.isValid())
    obj.insert("active_until", loungeOverlayUntilUtc_.toString(Qt::ISODate));
  return obj;
}

void MainDock::syncDiscord(bool postToDiscord, bool resetMatch) {
  if (!discord_ || !postDiscord_ || !postDiscord_->isChecked())
    return;
  if (!hasTestDiscordEndpoint_ && !QSslSocket::supportsSsl()) {
    setStatus(tr("TLS機能を読み込めないためDiscordへ投稿できません。"), "#ffb74d");
    return;
  }
  QString endpointStr = QString::fromUtf8(QUICKSCORE_EMBEDDED_BOT_ENDPOINT).trimmed();
  QString ingestKey = QString::fromUtf8(QUICKSCORE_EMBEDDED_BOT_INGEST_KEY).trimmed();
  if (hasTestDiscordEndpoint_) {
    if (endpointStr.isEmpty())
      endpointStr = QStringLiteral("http://127.0.0.1/api/v1/lounge/ingest");
    if (ingestKey.isEmpty())
      ingestKey = QStringLiteral("test-ingest-key");
  }
  const auto payload = buildLoungePayload(postToDiscord, resetMatch);
  discord_->sendLounge(QUrl(endpointStr), ingestKey, payload,
                       [this](bool ok, const QString &message) {
                         if (!ok)
                           setStatus(message, "#ffb74d");
                       });
}

void MainDock::syncDiscordCourse(const QString &course) {
  if (course.isEmpty() || !discord_ || !postDiscord_ || !postDiscord_->isChecked())
    return;
  if (!hasTestDiscordEndpoint_ && !QSslSocket::supportsSsl())
    return;
  QString endpointStr = QString::fromUtf8(QUICKSCORE_EMBEDDED_BOT_ENDPOINT).trimmed();
  QString ingestKey = QString::fromUtf8(QUICKSCORE_EMBEDDED_BOT_INGEST_KEY).trimmed();
  if (hasTestDiscordEndpoint_) {
    if (endpointStr.isEmpty())
      endpointStr = QStringLiteral("http://127.0.0.1/api/v1/lounge/ingest");
    if (ingestKey.isEmpty())
      ingestKey = QStringLiteral("test-ingest-key");
  }
  lastSentCourse_ = course;
  auto payload = buildLoungePayload(true);
  payload.insert("event", QStringLiteral("course"));
  payload.insert("course", course);
  discord_->sendLounge(QUrl(endpointStr), ingestKey, payload,
                       [this](bool ok, const QString &message) {
                         if (!ok)
                           setStatus(message, "#ffb74d");
                       });
}

void MainDock::saveSettings() {
  QSettings s(configDirectory_ + "/settings.ini", QSettings::IniFormat);
  s.setValue("schema", 11);
  s.setValue("sourceKey", source_->currentData());
  s.setValue("tags", tags_->text());
  s.setValue("discordUserId", configuredDiscordUserId());
  s.setValue("postToDiscordChannel", postDiscord_->isChecked());
}

void MainDock::loadSettings() {
  QSettings s(configDirectory_ + "/settings.ini", QSettings::IniFormat);
  tags_->setText(s.value("tags", "A, B, C, D").toString());
  if (discordUserId_)
    discordUserId_->setText(s.value("discordUserId", "").toString());
  updateDetectedFormat();
  savedSource_ = s.value("schema", 0).toInt() >= 5 ? s.value("sourceKey").toString() : QString{};
  postDiscord_->setChecked(s.value("postToDiscordChannel", false).toBool());
}

void MainDock::setStatus(const QString &text, const QString &color) {
  status_->setText(text);
  status_->setStyleSheet("font-weight:600;color:" + color + ";");
}
} // namespace quickscore
