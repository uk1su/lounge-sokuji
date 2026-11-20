#pragma once
#include "analysis.hpp"
#include "course.hpp"
#include "discord-sender.hpp"
#include "frame-capture.hpp"
#include "jev-api-reader.hpp"
#include "overlay.hpp"
#include "overlay-server.hpp"
#include "result-gate.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QWidget>
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
namespace quickscore {
class ResultPreviewWidget final : public QWidget {
  Q_OBJECT
public:
  explicit ResultPreviewWidget(QWidget *parent = nullptr);

  void setPlaceholderText(const QString &text);
  void setPreview(const QImage &frame, const std::vector<Standing> &standings,
                  const std::vector<std::string> &teamTags,
                  const std::vector<RowEvidence> &evidence = {},
                  const StandingsScoreRead &cumulative = {});
  void setEditing(bool editing, bool cumulativeMode = false);
  bool isEditing() const { return editing_; }
  bool hasPreview() const { return !cropped_.isNull(); }
  bool hasTagStrip() const { return standings_.size() == 12; }
  void setRowTag(int row, const QString &tag);
  QString rowTag(int row) const;
  std::vector<Standing> standings() const { return standings_; }
  void setOnEdited(std::function<void()> cb) { onEdited_ = std::move(cb); }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  int hitRow(const QPoint &pos) const;
  QString tooltipForRow(int row) const;

  QString placeholder_;
  QImage cropped_;
  std::vector<Standing> standings_;
  std::vector<std::string> teamTags_;
  std::vector<RowEvidence> evidence_;
  StandingsScoreRead cumulative_;
  bool editing_ = false;
  bool cumulativeMode_ = false;
  int hoveredRow_ = -1;
  QRect drawRect_;
  std::array<QRect, 12> badgeRects_{};
  std::function<void()> onEdited_;
};

class MainDock final : public QWidget {
public:
  explicit MainDock(QString configDirectory, QWidget *parent = nullptr,
                    QUrl testDiscordEndpoint = {}, QUrl testApiEndpoint = {});
  ~MainDock() override;

  quint16 overlayPort() const { return overlayServer_ ? overlayServer_->port() : 0; }

private:
  enum class EditMode { None, NewRace, ExistingRace, PendingFirstRace, CumulativeReconcile };

  void refreshSources();
  std::vector<std::string> configuredTags() const;
  int configuredPlayersPerTeam() const;
  void updateDetectedFormat();
  void startAggregation();
  void stopAggregation(bool autoFinished = false, bool syncOverlay = true);
  bool checkAutoFinish(bool syncOverlay = true);
  void receiveFrame(Frame frame);
  void receiveAnalysis(Analysis result, uint64_t generation, qint64 duration,
                       QDateTime frameReceivedAt);
  void workerLoop();
  void review(const Analysis &analysis, EditMode mode = EditMode::NewRace, int raceIndex = -1);
  void confirmPending();
  void cancelReview();
  bool commit(const std::vector<Standing> &standings, bool corrected, const Analysis &raw,
              QString *eventNote = nullptr, std::optional<QString> courseOverride = std::nullopt);
  void updateScores();
  void updateCourseDisplay();
  void clearCurrentCourse();
  QString editedReviewCourse() const;
  void refreshRaceHistory();
  void openSelectedRaceEditor();
  std::vector<Standing> currentEditorStandings() const;
  void updatePreview(const QImage &frame, const std::vector<Standing> &standings = {});
  void saveSettings();
  void loadSettings();
  void resetSession();
  void setStatus(const QString &text, const QString &color = QStringLiteral("#80cbc4"));
  void requestRecognition(const Analysis &captured);
  void requestStandingsVerification(const Analysis &captured);
  void requestRetryRecognition();
  void learnOverridesFromReview(const std::vector<Standing> &confirmed);
  void syncDiscord(bool postToDiscord = true, bool resetMatch = false);
  void syncDiscordCourse(const QString &course);
  QJsonObject buildLoungePayload(bool postToDiscord, bool resetMatch = false) const;

  QString configDirectory_, savedSource_;
  QComboBox *source_{}, *raceSelector_{};
  QLineEdit *tags_{}, *reviewCourse_{};
  QLabel *detectedFormat_{};
  std::unique_ptr<JevApiReader> api_;
  bool requestIssued_ = false;
  uint64_t requestCycle_ = 0;
  QCheckBox *postDiscord_{};
  std::unique_ptr<DiscordSender> discord_;
  bool hasTestDiscordEndpoint_ = false;
  QLabel *status_{}, *diagnostic_{}, *courseLabel_{}, *scores_{}, *reviewHelp_{};
  QWidget *reviewCourseRow_{};
  ResultPreviewWidget *preview_{};
  QPushButton *start_{}, *stop_{}, *refresh_{}, *confirm_{}, *cancel_{}, *retryRead_{}, *editRace_{};
  std::unique_ptr<FrameCapture> capture_;
  std::unique_ptr<Analyzer> analyzer_;
  std::unique_ptr<OverlayServer> overlayServer_;
  uint64_t overlayRevision_ = 0;
  Session session_;
  QJsonArray audit_;
  QJsonObject resultCapture_, pendingFirstAudit_;
  ResultGate gate_;
  CourseGate courseGate_;
  QString currentCourse_, lastSentCourse_;
  std::vector<std::pair<int, QString>> heldRaces_;
  Analysis latest_, pending_;
  std::vector<TagSample> playerOverrides_;
  std::vector<Standing> reviewBaselineStandings_;
  QImage latestImage_, lastCommittedFrame_;
  std::vector<Standing> lastCommittedStandings_;
  QElapsedTimer clock_;
  bool running_ = false, reviewing_ = false, storageOk_ = true;
  bool loungeOverlayActive_ = false;
  QDateTime loungeOverlayUntilUtc_;
  EditMode editMode_ = EditMode::None;
  int editingRaceIndex_ = -1;
  bool userTouchedReview_ = false;
  bool retryInFlight_ = false;
  qint64 reviewOpenedMs_ = 0;
  bool standingsRequestActive_ = false;
  int standingsStableCount_ = 0;
  qint64 standingsFirstMs_ = 0;
  int standingsLastTotal_ = -1;
  std::array<int, 12> standingsStableRows_{};
  int standingsCheckedRace_ = -1;
  int standingsCheckedTotal_ = -1;
  int verifiedRaceCount_ = 0;
  uint64_t generation_ = 0;
  qint64 lastFrameMs_ = 0, lastAnalysisMs_ = 0, lastPreviewMs_ = 0;
  std::atomic_bool frameQueued_{false};
  std::atomic_bool workerBusy_{false};
  std::mutex workerMutex_;
  std::condition_variable workerCv_;
  std::thread worker_;
  bool workerStop_ = false, jobReady_ = false, busy_ = false;
  QImage jobImage_;
  QDateTime jobFrameReceivedAt_, latestFrameReceivedAt_;
  std::vector<std::string> jobTags_;
  uint64_t jobGeneration_ = 0;
};
} // namespace quickscore
