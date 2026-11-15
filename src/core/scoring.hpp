#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace quickscore {

constexpr std::array<int, 12> kMarioKartPoints{{15, 12, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1}};

struct Standing {
  int rank{};
  std::string tag;
  double confidence{1.0};
};

struct ValidationResult {
  bool ok{};
  std::vector<std::string> errors;
};

struct RaceResult {
  int number{};
  int raceSpan{1};
  std::vector<Standing> standings;
  std::map<std::string, int> points;
  bool corrected{};
  std::string course;
  std::string label;
};

std::map<std::string, int> scoreRace(const std::vector<Standing> &standings);
ValidationResult validateRace(const std::vector<Standing> &standings,
                              const std::vector<std::string> &teamTags,
                              int playersPerTeam,
                              double minimumConfidence = 0.55);

class Session {
public:
  void configure(std::vector<std::string> tags, int playersPerTeam);
  ValidationResult validate(const std::vector<Standing> &standings) const;
  bool commit(std::vector<Standing> standings, bool corrected, ValidationResult *failure = nullptr,
              std::string course = {});
  bool replaceRace(size_t index, std::vector<Standing> standings, bool corrected,
                   ValidationResult *failure = nullptr);
  void deferFirstRace(std::vector<Standing> partialStandings, std::string course = {});
  bool resolvePendingFirstRaceManually(std::vector<Standing> standings, bool corrected,
                                       ValidationResult *failure = nullptr);
  bool reconcileWithCumulativeTotals(const std::map<std::string, int> &gameTotals,
                                     int gameRaceCount, bool *changed = nullptr);
  bool importPriorSummary(int priorRaces, const std::map<std::string, int> &priorTotals,
                          std::string label = {});
  bool restoreRaceAndAdjustments(RaceResult race, bool prepend);
  void setTeamAdjustments(std::map<std::string, int> adjustments);
  void setInGameScoreOffset(int offset);
  void addInGameScoreOffset(int delta);
  bool undo();
  void reset();

  const std::vector<std::string> &tags() const { return tags_; }
  int playersPerTeam() const { return playersPerTeam_; }
  const std::vector<RaceResult> &races() const { return races_; }
  bool hasAutoConfirmedRace() const;
  bool hasPendingFirstRace() const { return pendingFirstRace_; }
  const std::vector<Standing> &pendingFirstRaceStandings() const {
    return pendingFirstRaceStandings_;
  }
  int inGameScoreOffset() const { return inGameScoreOffset_; }
  size_t effectiveRaceCount() const;
  std::map<std::string, int> committedTotals() const;
  const std::map<std::string, int> &teamAdjustments() const { return teamAdjustments_; }
  std::map<std::string, int> totals() const;
  std::string toJson() const;

private:
  void renumberRaces();

  std::vector<std::string> tags_;
  int playersPerTeam_{3};
  std::vector<RaceResult> races_;
  std::map<std::string, int> teamAdjustments_;
  int inGameScoreOffset_{0};
  bool pendingFirstRace_{false};
  std::vector<Standing> pendingFirstRaceStandings_;
  std::string pendingFirstRaceCourse_;
};

} // namespace quickscore
