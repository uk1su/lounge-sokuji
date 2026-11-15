#pragma once

#include <cstdint>
#include <vector>

namespace quickscore {

void registerScoreboardSource();
void updateScoreboardImage(int width, int height, std::vector<uint8_t> rgba);

} // namespace quickscore
