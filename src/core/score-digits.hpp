#pragma once

#include <QImage>
#include <array>

namespace quickscore {

struct RowScoreRead {
  int points{-1};
  double minConfidence{0.0};
  double minMargin{0.0};
  bool ok() const { return points >= 0; }
};

struct StandingsScoreRead {
  std::array<RowScoreRead, 12> rows{};
  int totalPoints{0};
  int validRows{0};
  bool nonIncreasing{false};

  bool valid() const {
    return validRows == 12 && nonIncreasing && totalPoints > 0 && (totalPoints % 82) == 0;
  }

  bool hasSortedTotals() const { return validRows == 12 && nonIncreasing && totalPoints > 0; }

  int impliedRaceCount() const { return valid() ? totalPoints / 82 : 0; }
};

StandingsScoreRead readCumulativeScores(const QImage &frame1920);

} // namespace quickscore
