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

ValidationResult Session::validate(const std::vector<Standing> &standings) const
{
  return validateRace(standings, tags_, playersPerTeam_);
}

bool Session::commit(std::vector<Standing> standings, bool corrected, ValidationResult *failure)
{
  auto validation = validate(standings);
  if (!validation.ok) {
    if (failure) *failure = std::move(validation);
    return false;
  }
  RaceResult race;
  race.number = static_cast<int>(races_.size()) + 1;
  race.points = scoreRace(standings);
  race.standings = std::move(standings);
  race.corrected = corrected;
  races_.push_back(std::move(race));
  return true;
}

bool Session::undo()
{
  if (races_.empty()) return false;
  races_.pop_back();
  return true;
}

void Session::reset() { races_.clear(); }

std::map<std::string, int> Session::totals() const
{
  std::map<std::string, int> result;
  for (const auto &tag : tags_) result[tag] = 0;
  for (const auto &race : races_)
    for (const auto &entry : race.points) result[entry.first] += entry.second;
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
  out << "],\n  \"races\": [";
  for (size_t i = 0; i < races_.size(); ++i) {
    const auto &race = races_[i];
    if (i) out << ',';
    out << "\n    {\"number\": " << race.number << ", \"corrected\": "
        << (race.corrected ? "true" : "false") << ", \"standings\": [";
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

} // namespace quickscore
