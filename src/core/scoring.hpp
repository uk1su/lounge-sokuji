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
  std::vector<Standing> standings;
  std::map<std::string, int> points;
  bool corrected{};
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
  bool commit(std::vector<Standing> standings, bool corrected, ValidationResult *failure = nullptr);
  bool undo();
  void reset();

  const std::vector<std::string> &tags() const { return tags_; }
  int playersPerTeam() const { return playersPerTeam_; }
  const std::vector<RaceResult> &races() const { return races_; }
  std::map<std::string, int> totals() const;
  std::string toJson() const;

private:
  std::vector<std::string> tags_;
  int playersPerTeam_{3};
  std::vector<RaceResult> races_;
};

} // namespace quickscore
