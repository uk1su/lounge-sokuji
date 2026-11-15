#pragma once
#include "analysis.hpp"

namespace quickscore {
// Uses the reference plugin's mean-centered normalized correlation, with separate
// first-character and wider-prefix checks. Samples are learned only from confirmation.
std::vector<TagSample> confirmedSamples(const QImage &image,
                                        const std::vector<Standing> &confirmed);
Analysis readFast(Analysis layout, const std::vector<std::string> &tags,
                  const std::vector<TagSample> &samples);
} // namespace quickscore
