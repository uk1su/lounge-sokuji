#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace quickscore {

std::string recognizeNativeText(const std::vector<uint8_t> &gray, int width, int height);
std::string matchKnownTag(const std::string &recognized, const std::vector<std::string> &tags);

} // namespace quickscore
