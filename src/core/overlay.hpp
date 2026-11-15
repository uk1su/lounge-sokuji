#pragma once

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace quickscore {

struct OverlayTeamEntry {
  std::string tag;
  int score{};
  int originalIndex{};
};

struct OverlayState {
  int teamCount{};
  int raceCount{};
  int totalRaces{12};
  int remainingRaces{12};
  bool winConfirmed{};
  std::vector<OverlayTeamEntry> teamsOriginal;
  std::vector<OverlayTeamEntry> teamsSorted;
  uint64_t revision{};
};

/// Build normalized overlay state sorted identically to mk8dx_sokuji.
OverlayState buildOverlayState(const std::vector<std::string> &tags,
                               const std::map<std::string, int> &totals,
                               size_t raceCount,
                               int totalRaces = 12,
                               uint64_t revision = 0);

/// Serialize OverlayState to the JSON payload consumed by overlay.html and state.json.
QJsonObject overlayStateToJson(const OverlayState &state);

/// Serialize OverlayState as a standalone JavaScript assignment for file:// polling.
QByteArray overlayStateToJs(const OverlayState &state);

/// Render the 840x160 transparent RGBA scoreboard matching mk8dx_sokuji/overlay.html.
QImage renderOverlayImage(const OverlayState &state);

} // namespace quickscore
