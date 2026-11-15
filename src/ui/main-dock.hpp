#pragma once
#include "analysis.hpp"
#include "credential-store.hpp"
#include "discord-sender.hpp"
#include "frame-capture.hpp"
#include "jev-api-reader.hpp"
#include "result-gate.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QWidget>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;
class QCheckBox;
namespace quickscore {
class MainDock final : public QWidget {
public:
  explicit MainDock(QString configDirectory, QWidget *parent = nullptr,
                    QUrl testDiscordEndpoint = {}, QUrl testApiEndpoint = {});
  ~MainDock() override;

private:
  void refreshSources();
  void applyFormat();
  std::vector<std::string> configuredTags() const;
  void startStop();
  void receiveFrame(Frame frame);
  void receiveAnalysis(Analysis result, uint64_t generation, qint64 duration,
                       QDateTime frameReceivedAt);
  void workerLoop();
  void review(const Analysis &analysis);
  void confirmPending();
  void cancelReview();
  bool commit(const std::vector<Standing> &standings, bool corrected, const Analysis &raw);
  void updateScores();
  void saveSettings();
  void loadSettings();
  void undo();
  void resetSession();
  void setStatus(const QString &text, const QString &color = QStringLiteral("#80cbc4"));
  void updateRowPreview(int row);
  void exportDiagnostic();
  void requestRecognition(const Analysis &captured);
  void requestJevAssistance();
  void updateFastStatus();
  void learnConfirmed(const Analysis &raw, const std::vector<Standing> &standings);
  void sendDiscord();
  void updateDiscord();
  QString configDirectory_, savedSource_;
  QComboBox *source_{}, *format_{};
  QLineEdit *tags_{};
  QCheckBox *autoCommit_{};
  std::unique_ptr<JevApiReader> api_;
  QCheckBox *apiEnabled_{};
  QLineEdit *apiKey_{};
  QProcess localSetup_;
  QPushButton *setupLocal_{};
  QLabel *localStatus_{};
  QLabel *fastStatus_{};
  QPushButton *jevAssist_{}, *clearFast_{};
  std::vector<TagSample> fastSamples_, jobSamples_;
  QString fastSource_;
  QByteArray localSetupOutput_;
  bool requestIssued_ = false;
  uint64_t requestCycle_ = 0;
  QImage stableNames_;
  QLineEdit *discordToken_{}, *discordChannel_{};
  QLabel *discordStatus_{};
  QPushButton *discordSave_{}, *discordDelete_{}, *discordSend_{};
  std::unique_ptr<DiscordSender> discord_;
  bool discordCooldown_ = false;
  QLabel *status_{}, *diagnostic_{}, *preview_{}, *scores_{}, *reviewHelp_{};
  QPushButton *start_{}, *refresh_{}, *manual_{}, *undo_{}, *confirm_{}, *cancel_{};
  QTableWidget *correction_{};
  std::unique_ptr<FrameCapture> capture_;
  std::unique_ptr<Analyzer> analyzer_;
  Session session_;
  QJsonArray audit_;
  QJsonObject resultCapture_;
  QLabel *captureStatus_{};
  ResultGate gate_;
  Analysis latest_, pending_;
  QImage latestImage_;
  QElapsedTimer clock_;
  bool running_ = false, reviewing_ = false, storageOk_ = true;
  uint64_t generation_ = 0;
  qint64 lastFrameMs_ = 0, lastAnalysisMs_ = 0;
  std::atomic_bool frameQueued_{false};
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
