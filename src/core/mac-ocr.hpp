#pragma once

#include <cstdint>
#include <string>

namespace quickscore {

std::string macRecognizeBgra(const uint8_t *bgra, int width, int height);
std::string macRecognizeGray(const uint8_t *gray, int width, int height);

} // namespace quickscore
