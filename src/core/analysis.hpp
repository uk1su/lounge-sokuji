#pragma once
#include "../reference/recognizer.hpp"
#include "score-digits.hpp"
#include "scoring.hpp"
#include <QImage>
#include <QString>

namespace quickscore {
struct TagSample {
  std::string tag;
  QImage patch;
};
struct CharBox {
  int left = 15;
  int width = 36;
};
struct TagCandidate {
  std::string tag;
  double score = 0;
  QString method;
};
struct RowEvidence {
  QString text, method;
  double similarity{};
  std::string closestTag;
  double closestScore{};
};
struct Analysis {
  QImage image;
  mk::Result layout;
  std::vector<Standing> standings;
  std::vector<RowEvidence> evidence;
  StandingsScoreRead cumulativeScores;
  QString detectedCourse;
  QString error;
  bool finished_in_twelfth = false;
  bool complete() const {
    if (finished_in_twelfth) {
      return layout.anchors >= 11 && layout.pluses >= 9;
    }
    return layout.anchors == 12 && layout.pluses >= 10;
  }
  bool isCumulativeStandings() const {
    return layout.anchors == 12 && layout.pluses <= 2 && cumulativeScores.hasSortedTotals();
  }
};
class Analyzer {
public:
  explicit Analyzer(const QString &templates);
  Analysis read(const QImage &source, const std::vector<std::string> &tags,
                const std::vector<TagSample> &samples, bool ocr = true,
                bool layoutOnly = false) const;
  static QImage rowImage(const QImage &source, int row);
  static std::pair<std::string, double> matchSamples(const QImage &row,
                                                     const std::vector<std::string> &tags,
                                                     const std::vector<TagSample> &samples);

private:
  mk::Recognizer reference_;
};
CharBox detectFirstCharBox(const QImage &rowCrop);
double charSimilarity(const QString &c1, const QString &c2);
QImage renderCharTemplate(const QString &character, int targetHeight = 24);
double matchCharTemplate(const QImage &charCrop, const QImage &tmpl);
TagCandidate matchClosestTag(const std::string &ocrText, const QImage &rowCrop, double itfScore,
                             const std::vector<std::string> &tags,
                             const std::vector<TagSample> &samples);
std::vector<std::string> resolveTeamsWithCapacity(
    const std::vector<std::vector<std::pair<std::string, double>>> &rowScores,
    int playersPerTeam);
std::string exactTag(const std::string &text, const std::vector<std::string> &tags);
QString initialIdentifier(const QString &s);
bool validateTeamTags(const std::vector<std::string> &tags, QString *error = nullptr);
bool saveSamples(const QString &path, const std::vector<TagSample> &samples, QString &error);
bool loadSamples(const QString &path, std::vector<TagSample> &samples, QString &error);
QImage extractPlayerNamePatch(const QImage &frame1920, int row);
double matchPlayerNameEdge(const QImage &frame1920, int row, const QImage &namePatch);
void applyPlayerOverrides(Analysis &analysis, const std::vector<TagSample> &overrides,
                          const std::vector<std::string> &activeTags);
bool resolveSingleDeficitRow(Analysis &analysis, const std::vector<std::string> &tags,
                             int playersPerTeam);
void enforceTwelfthFinishNotice(Analysis &analysis, const std::vector<std::string> &tags);
QString formatScoreLine(const std::vector<std::string> &tags,
                        const std::map<std::string, int> &totals,
                        size_t raceCount,
                        int totalRaces = 12);
} // namespace quickscore
