#include "scoring.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>

namespace quickscore {
namespace {
std::string escapeJson(const std::string &value)
{
  std::ostringstream out;
  for (const unsigned char ch : value) {
    switch (ch) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default:
      if (ch < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(ch);
      else out << ch;
    }
  }
  return out.str();
}
bool trySolveUnknownStandings(const std::vector<Standing> &partial,
                              const std::map<std::string, int> &targetPoints,
                              const std::vector<std::string> &tags,
                              int playersPerTeam,
                              std::vector<Standing> &solved)
{
  if (partial.size() != 12) return false;
  const std::set<std::string> allowed(tags.begin(), tags.end());
  std::vector<size_t> unknownIndices;
  std::map<std::string, int> neededCount;
  std::map<std::string, int> neededPoints = targetPoints;
  for (const auto &t : tags) neededCount[t] = playersPerTeam;

  solved = partial;
  for (size_t i = 0; i < 12; ++i) {
    solved[i].rank = int(i + 1);
    const int pts = kMarioKartPoints[i];
    const auto &tag = partial[i].tag;
    if (!tag.empty() && allowed.count(tag) && partial[i].confidence >= 0.75 &&
        neededCount[tag] > 0 && neededPoints[tag] >= pts) {
      --neededCount[tag];
      neededPoints[tag] -= pts;
    } else {
      solved[i].tag.clear();
      unknownIndices.push_back(i);
    }
  }

  auto dfs = [&](auto &self, size_t uIdx) -> bool {
    if (uIdx == unknownIndices.size()) {
      for (const auto &t : tags) {
        if (neededCount[t] != 0 || neededPoints[t] != 0) return false;
      }
      return true;
    }
    const size_t row = unknownIndices[uIdx];
    const int pts = kMarioKartPoints[row];
    for (const auto &t : tags) {
      if (neededCount[t] <= 0 || neededPoints[t] < pts) continue;
      --neededCount[t];
      neededPoints[t] -= pts;
      solved[row].tag = t;
      solved[row].confidence = 1.0;
      if (self(self, uIdx + 1)) return true;
      neededPoints[t] += pts;
      ++neededCount[t];
      solved[row].tag.clear();
    }
    return false;
  };
  return dfs(dfs, 0);
}
} // namespace

std::map<std::string, int> scoreRace(const std::vector<Standing> &standings)
{
  std::map<std::string, int> result;
  for (const auto &standing : standings) {
    if (standing.rank >= 1 && standing.rank <= 12 && !standing.tag.empty())
      result[standing.tag] += kMarioKartPoints[static_cast<size_t>(standing.rank - 1)];
  }
  return result;
}

ValidationResult validateRace(const std::vector<Standing> &standings,
                              const std::vector<std::string> &teamTags,
                              int playersPerTeam,
                              double minimumConfidence)
{
  ValidationResult result{true, {}};
  const auto fail = [&](std::string message) {
    result.ok = false;
    result.errors.emplace_back(std::move(message));
  };

  if (standings.size() != 12) fail("12人分の順位がありません");
  if (teamTags.empty() || playersPerTeam <= 0 || static_cast<int>(teamTags.size()) * playersPerTeam != 12)
    fail("対戦形式とチーム数が12人構成になっていません");

  std::set<int> ranks;
  std::map<std::string, int> counts;
  const std::set<std::string> allowed(teamTags.begin(), teamTags.end());
  for (const auto &standing : standings) {
    if (standing.rank < 1 || standing.rank > 12 || !ranks.insert(standing.rank).second)
      fail("順位1〜12に重複または欠落があります");
    if (allowed.find(standing.tag) == allowed.end())
      fail("未登録のタグがあります: " + standing.tag);
    else
      ++counts[standing.tag];
    if (!std::isfinite(standing.confidence) || standing.confidence > 1.0 || standing.confidence < minimumConfidence)
      fail(std::to_string(standing.rank) + "位の認識信頼度が低すぎます");
  }
  for (const auto &tag : teamTags) {
    if (counts[tag] != playersPerTeam)
      fail("タグ " + tag + " の人数が " + std::to_string(playersPerTeam) + " 人ではありません");
  }

  const auto scored = scoreRace(standings);
  const int total = std::accumulate(scored.begin(), scored.end(), 0,
                                    [](int sum, const auto &entry) { return sum + entry.second; });
  if (total != 82) fail("得点合計が82点ではありません");

  std::sort(result.errors.begin(), result.errors.end());
  result.errors.erase(std::unique(result.errors.begin(), result.errors.end()), result.errors.end());
  return result;
}

void Session::configure(std::vector<std::string> tags, int playersPerTeam)
{
  tags_ = std::move(tags);
  playersPerTeam_ = playersPerTeam;
}

bool Session::renameTags(const std::vector<std::string> &newTags)
{
  if (tags_.empty() || newTags.size() != tags_.size())
    return false;
  std::set<std::string> uniqueCheck;
  for (const auto &t : newTags) {
    if (t.empty() || !uniqueCheck.insert(t).second)
      return false;
  }
  std::map<std::string, std::string> remap;
  for (size_t i = 0; i < tags_.size(); ++i)
    remap[tags_[i]] = newTags[i];

  const auto mapTag = [&](std::string &tag) {
    auto it = remap.find(tag);
    if (it != remap.end())
      tag = it->second;
  };
  const auto mapScores = [&](const std::map<std::string, int> &in) {
    std::map<std::string, int> out;
    for (const auto &[k, v] : in) {
      auto it = remap.find(k);
      out[it != remap.end() ? it->second : k] += v;
    }
    return out;
  };

  for (auto &race : races_) {
    for (auto &s : race.standings)
      mapTag(s.tag);
    race.points = mapScores(race.points);
  }
  for (auto &s : pendingFirstRaceStandings_)
    mapTag(s.tag);
  teamAdjustments_ = mapScores(teamAdjustments_);
  tags_ = newTags;
  return true;
}

bool Session::hasAutoConfirmedRace() const
{
  return std::any_of(races_.begin(), races_.end(), [](const RaceResult &race) {
    return race.raceSpan == 1 && !race.corrected && race.standings.size() == 12;
  });
}

ValidationResult Session::validate(const std::vector<Standing> &standings) const
{
  return validateRace(standings, tags_, playersPerTeam_);
}

bool Session::commit(std::vector<Standing> standings, bool corrected, ValidationResult *failure,
                     std::string course)
{
  auto validation = validate(standings);
  if (!validation.ok) {
    if (failure) *failure = std::move(validation);
    return false;
  }
  RaceResult race;
  race.raceSpan = 1;
  race.number = static_cast<int>(effectiveRaceCount()) + 1;
  race.points = scoreRace(standings);
  race.standings = std::move(standings);
  race.corrected = corrected;
  race.course = std::move(course);
  races_.push_back(std::move(race));
  renumberRaces();
  return true;
}

bool Session::replaceRace(size_t index, std::vector<Standing> standings, bool corrected,
                          ValidationResult *failure)
{
  if (index >= races_.size() || races_[index].raceSpan != 1) return false;
  auto validation = validate(standings);
  if (!validation.ok) {
    if (failure) *failure = std::move(validation);
    return false;
  }
  races_[index].points = scoreRace(standings);
  races_[index].standings = std::move(standings);
  races_[index].corrected = corrected;
  teamAdjustments_.clear();
  renumberRaces();
  return true;
}

bool Session::setRaceCourse(size_t index, std::string course)
{
  if (index >= races_.size() || races_[index].raceSpan != 1) return false;
  races_[index].course = std::move(course);
  return true;
}

void Session::deferFirstRace(std::vector<Standing> partialStandings, std::string course)
{
  if (!races_.empty() || pendingFirstRace_) return;
  pendingFirstRace_ = true;
  pendingFirstRaceStandings_ = std::move(partialStandings);
  pendingFirstRaceCourse_ = std::move(course);
}

bool Session::resolvePendingFirstRaceManually(std::vector<Standing> standings, bool corrected,
                                              ValidationResult *failure)
{
  if (!pendingFirstRace_) return false;
  auto validation = validate(standings);
  if (!validation.ok) {
    if (failure) *failure = std::move(validation);
    return false;
  }
  RaceResult race;
  race.number = 1;
  race.points = scoreRace(standings);
  race.standings = std::move(standings);
  race.corrected = corrected;
  race.course = std::move(pendingFirstRaceCourse_);
  races_.insert(races_.begin(), std::move(race));
  pendingFirstRace_ = false;
  pendingFirstRaceStandings_.clear();
  pendingFirstRaceCourse_.clear();
  teamAdjustments_.clear();
  renumberRaces();
  return true;
}

bool Session::reconcileWithCumulativeTotals(const std::map<std::string, int> &gameTotals,
                                            int gameRaceCount, bool *changed)
{
  if (changed) *changed = false;
  if (tags_.empty() || gameRaceCount <= 0 || gameTotals.size() != tags_.size()) return false;
  int sum = 0;
  for (const auto &t : tags_) {
    const auto it = gameTotals.find(t);
    if (it == gameTotals.end() || it->second < 0) return false;
    sum += it->second;
  }
  if (sum != 82 * gameRaceCount) return false;

  if (pendingFirstRace_ && gameRaceCount == static_cast<int>(effectiveRaceCount())) {
    const auto committed = committedTotals();
    std::map<std::string, int> race1Points;
    bool allNonNegative = true;
    for (const auto &t : tags_) {
      const int diff = gameTotals.at(t) - (committed.count(t) ? committed.at(t) : 0);
      race1Points[t] = diff;
      if (diff < 0) allNonNegative = false;
    }

    RaceResult r1;
    r1.number = 1;
    r1.raceSpan = 1;
    r1.corrected = true;
    r1.course = std::move(pendingFirstRaceCourse_);
    if (allNonNegative) {
      std::vector<Standing> solved;
      if (trySolveUnknownStandings(pendingFirstRaceStandings_, race1Points, tags_, playersPerTeam_,
                                   solved)) {
        r1.standings = std::move(solved);
        r1.points = scoreRace(r1.standings);
      } else {
        r1.standings = pendingFirstRaceStandings_;
        r1.points = std::move(race1Points);
      }
      teamAdjustments_.clear();
    } else {
      for (const auto &t : tags_) r1.points[t] = 0;
      r1.standings = pendingFirstRaceStandings_;
      for (const auto &t : tags_) {
        const int adj = gameTotals.at(t) - (committed.count(t) ? committed.at(t) : 0);
        if (adj != 0) teamAdjustments_[t] = adj;
      }
    }

    races_.insert(races_.begin(), std::move(r1));
    pendingFirstRace_ = false;
    pendingFirstRaceStandings_.clear();
    pendingFirstRaceCourse_.clear();
    renumberRaces();
    if (changed) *changed = true;
    return true;
  }

  if (!pendingFirstRace_ && races_.size() == 1 && races_[0].raceSpan == 1 &&
      !races_[0].corrected && inGameScoreOffset_ == 0 && gameRaceCount >= 2) {
    const auto &firstPts = races_[0].points;
    std::map<std::string, int> priorPoints;
    bool ok = true;
    int priorSum = 0;
    for (const auto &t : tags_) {
      const int committedPt = firstPts.count(t) ? firstPts.at(t) : 0;
      const int priorPt = gameTotals.at(t) - committedPt;
      if (priorPt < 0) {
        ok = false;
        break;
      }
      priorPoints[t] = priorPt;
      priorSum += priorPt;
    }
    const int priorRaces = gameRaceCount - 1;
    if (ok && priorSum == 82 * priorRaces) {
      RaceResult summary;
      summary.number = priorRaces;
      summary.raceSpan = priorRaces;
      summary.corrected = true;
      summary.points = std::move(priorPoints);
      summary.label = priorRaces == 1 ? std::string("1R合計")
                                      : ("1〜" + std::to_string(priorRaces) + "R合計");
      races_.insert(races_.begin(), std::move(summary));
      teamAdjustments_.clear();
      renumberRaces();
      if (changed) *changed = true;
      return true;
    }
  }

  if (!pendingFirstRace_ && inGameScoreOffset_ == 0 &&
      gameRaceCount == static_cast<int>(effectiveRaceCount())) {
    const auto current = totals();
    if (current == gameTotals) return true;
    const auto committed = committedTotals();
    teamAdjustments_.clear();
    for (const auto &t : tags_) {
      const int delta = gameTotals.at(t) - (committed.count(t) ? committed.at(t) : 0);
      if (delta != 0) teamAdjustments_[t] = delta;
    }
    if (changed) *changed = true;
    return true;
  }

  return false;
}

bool Session::importPriorSummary(int priorRaces, const std::map<std::string, int> &priorTotals,
                                 std::string label)
{
  if (priorRaces < 1 || !races_.empty() || pendingFirstRace_ || tags_.empty())
    return false;
  int sum = 0;
  std::map<std::string, int> cleanTotals;
  for (const auto &t : tags_) {
    const auto it = priorTotals.find(t);
    if (it == priorTotals.end() || it->second < 0)
      return false;
    cleanTotals[t] = it->second;
    sum += it->second;
  }
  if (sum != 82 * priorRaces)
    return false;

  RaceResult summary;
  summary.raceSpan = priorRaces;
  summary.number = priorRaces;
  summary.corrected = true;
  summary.points = std::move(cleanTotals);
  summary.label = !label.empty() ? std::move(label)
                                 : (priorRaces == 1
                                        ? std::string("1R合計")
                                        : ("1〜" + std::to_string(priorRaces) + "R合計"));
  races_.push_back(std::move(summary));
  teamAdjustments_.clear();
  renumberRaces();
  return true;
}

bool Session::restoreRaceAndAdjustments(RaceResult race, bool prepend)
{
  if (prepend) races_.insert(races_.begin(), std::move(race));
  else races_.push_back(std::move(race));
  renumberRaces();
  return true;
}

void Session::setTeamAdjustments(std::map<std::string, int> adjustments)
{
  teamAdjustments_ = std::move(adjustments);
}

void Session::setInGameScoreOffset(int offset)
{
  inGameScoreOffset_ = std::max(0, offset);
}

void Session::addInGameScoreOffset(int delta)
{
  inGameScoreOffset_ = std::max(0, inGameScoreOffset_ + delta);
}

size_t Session::effectiveRaceCount() const
{
  size_t count = pendingFirstRace_ ? 1 : 0;
  for (const auto &race : races_)
    count += static_cast<size_t>(std::max(1, race.raceSpan));
  return count;
}

void Session::renumberRaces()
{
  int nextRace = pendingFirstRace_ ? 2 : 1;
  for (auto &r : races_) {
    const int span = std::max(1, r.raceSpan);
    nextRace += span - 1;
    r.number = nextRace;
    ++nextRace;
  }
}

bool Session::undo()
{
  if (!races_.empty()) {
    races_.pop_back();
    teamAdjustments_.clear();
    return true;
  }
  if (pendingFirstRace_) {
    pendingFirstRace_ = false;
    pendingFirstRaceStandings_.clear();
    pendingFirstRaceCourse_.clear();
    teamAdjustments_.clear();
    return true;
  }
  return false;
}

void Session::reset()
{
  races_.clear();
  teamAdjustments_.clear();
  inGameScoreOffset_ = 0;
  pendingFirstRace_ = false;
  pendingFirstRaceStandings_.clear();
  pendingFirstRaceCourse_.clear();
}

std::map<std::string, int> Session::committedTotals() const
{
  std::map<std::string, int> result;
  for (const auto &tag : tags_) result[tag] = 0;
  for (const auto &race : races_)
    for (const auto &entry : race.points) result[entry.first] += entry.second;
  return result;
}

std::map<std::string, int> Session::totals() const
{
  auto result = committedTotals();
  for (const auto &entry : teamAdjustments_) result[entry.first] += entry.second;
  return result;
}

std::string Session::toJson() const
{
  std::ostringstream out;
  out << "{\n  \"playersPerTeam\": " << playersPerTeam_ << ",\n  \"teams\": [";
  for (size_t i = 0; i < tags_.size(); ++i) {
    if (i) out << ", ";
    out << '"' << escapeJson(tags_[i]) << '"';
  }
  out << "],\n  \"inGameScoreOffset\": " << inGameScoreOffset_
      << ",\n  \"pendingFirstRace\": " << (pendingFirstRace_ ? "true" : "false")
      << ",\n  \"pendingFirstRaceCourse\": \"" << escapeJson(pendingFirstRaceCourse_)
      << "\",\n  \"pendingFirstRaceStandings\": [";
  for (size_t j = 0; j < pendingFirstRaceStandings_.size(); ++j) {
    if (j) out << ',';
    const auto &s = pendingFirstRaceStandings_[j];
    out << "{\"rank\":" << s.rank << ",\"tag\":\"" << escapeJson(s.tag)
        << "\",\"confidence\":" << std::fixed << std::setprecision(3) << s.confidence << '}';
  }
  out << "],\n  \"teamAdjustments\": {";
  bool firstAdj = true;
  for (const auto &entry : teamAdjustments_) {
    if (!firstAdj) out << ',';
    firstAdj = false;
    out << '"' << escapeJson(entry.first) << "\": " << entry.second;
  }
  out << "},\n  \"races\": [";
  for (size_t i = 0; i < races_.size(); ++i) {
    const auto &race = races_[i];
    if (i) out << ',';
    out << "\n    {\"number\": " << race.number << ", \"raceSpan\": " << std::max(1, race.raceSpan)
        << ", \"label\": \"" << escapeJson(race.label) << "\", \"corrected\": "
        << (race.corrected ? "true" : "false") << ", \"course\": \""
        << escapeJson(race.course) << "\", \"points\": {";
    bool firstPt = true;
    for (const auto &entry : race.points) {
      if (!firstPt) out << ',';
      firstPt = false;
      out << '"' << escapeJson(entry.first) << "\": " << entry.second;
    }
    out << "}, \"standings\": [";
    for (size_t j = 0; j < race.standings.size(); ++j) {
      if (j) out << ',';
      const auto &s = race.standings[j];
      out << "{\"rank\":" << s.rank << ",\"tag\":\"" << escapeJson(s.tag)
          << "\",\"confidence\":" << std::fixed << std::setprecision(3) << s.confidence << '}';
    }
    out << "]}";
  }
  out << "\n  ]\n}\n";
  return out.str();
}

bool trySolveUnknownStandings(const std::vector<Standing> &partial,
                              const std::map<std::string, int> &targetPoints,
                              const std::vector<std::string> &tags,
                              int playersPerTeam,
                              std::vector<Standing> &solved)
{
  if (partial.size() != 12) return false;
  const std::set<std::string> allowed(tags.begin(), tags.end());
  std::vector<size_t> unknownIndices;
  std::map<std::string, int> neededCount;
  std::map<std::string, int> neededPoints = targetPoints;
  for (const auto &t : tags) neededCount[t] = playersPerTeam;

  solved = partial;
  for (size_t i = 0; i < 12; ++i) {
    solved[i].rank = int(i + 1);
    const int pts = kMarioKartPoints[i];
    const auto &tag = partial[i].tag;
    if (!tag.empty() && allowed.count(tag) && partial[i].confidence >= 0.75 &&
        neededCount[tag] > 0 && neededPoints[tag] >= pts) {
      --neededCount[tag];
      neededPoints[tag] -= pts;
    } else {
      solved[i].tag.clear();
      unknownIndices.push_back(i);
    }
  }

  auto dfs = [&](auto &self, size_t uIdx) -> bool {
    if (uIdx == unknownIndices.size()) {
      for (const auto &t : tags) {
        if (neededCount[t] != 0 || neededPoints[t] != 0) return false;
      }
      return true;
    }
    const size_t row = unknownIndices[uIdx];
    const int pts = kMarioKartPoints[row];
    for (const auto &t : tags) {
      if (neededCount[t] <= 0 || neededPoints[t] < pts) continue;
      --neededCount[t];
      neededPoints[t] -= pts;
      solved[row].tag = t;
      solved[row].confidence = 1.0;
      if (self(self, uIdx + 1)) return true;
      neededPoints[t] += pts;
      ++neededCount[t];
      solved[row].tag.clear();
    }
    return false;
  };
  return dfs(dfs, 0);
}
} // namespace quickscore
