#include "fast-reader.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace quickscore {
namespace {
constexpr int prefixWidth = 72, prefixHeight = 40, initialWidth = 36;
struct Pattern {
  std::string tag;
  QImage gray;
};
double variance(const QImage &image, int width) {
  double sum = 0, square = 0;
  int count = 0;
  for (int y = 0; y < image.height(); y += 2)
    for (int x = 0; x < width; x += 2) {
      const double value = image.constScanLine(y)[x];
      sum += value;
      square += value * value;
      ++count;
    }
  return square / count - std::pow(sum / count, 2);
}
double match(const QImage &gray, const QImage &sample, int width) {
  double templateSum = 0, templateSquare = 0;
  int count = 0;
  for (int y = 0; y < prefixHeight; y += 2)
    for (int x = 0; x < width; x += 2) {
      const double value = sample.constScanLine(y)[x];
      templateSum += value;
      templateSquare += value * value;
      ++count;
    }
  const double templateVariance = templateSquare - templateSum * templateSum / count;
  if (templateVariance / count < 64)
    return 0;
  double best = 0;
  // Search only around the name's actual start, never through a name suffix.
  for (int dy = 0; dy <= 6; ++dy)
    for (int dx = 0; dx <= 8; ++dx) {
      double sum = 0, square = 0, dot = 0;
      for (int y = 0; y < prefixHeight; y += 2)
        for (int x = 0; x < width; x += 2) {
          const double value = gray.constScanLine(y + dy)[x + dx];
          sum += value;
          square += value * value;
          dot += value * sample.constScanLine(y)[x];
        }
      const double currentVariance = square - sum * sum / count;
      if (currentVariance / count >= 64)
        best = std::max(best, std::abs((dot - sum * templateSum / count) /
                                       std::sqrt(currentVariance * templateVariance)));
    }
  return std::clamp(best, 0.0, 1.0);
}
} // namespace

std::vector<TagSample> confirmedSamples(const QImage &image,
                                        const std::vector<Standing> &confirmed) {
  std::vector<TagSample> samples;
  if (image.size() != QSize(1920, 1080) || confirmed.size() != 12)
    return samples;
  for (const auto &standing : confirmed) {
    if (standing.rank < 1 || standing.rank > 12 || standing.tag.empty())
      continue;
    auto patch = image.copy(1018, 90 + 78 * (standing.rank - 1), prefixWidth, prefixHeight)
                     .convertToFormat(QImage::Format_Grayscale8);
    if (variance(patch, initialWidth) >= 64)
      samples.push_back({standing.tag, std::move(patch)});
  }
  return samples;
}

Analysis readFast(Analysis result, const std::vector<std::string> &tags,
                  const std::vector<TagSample> &samples) {
  if (!result.complete() || result.image.size() != QSize(1920, 1080))
    return result;
  std::vector<Pattern> patterns;
  for (const auto &sample : samples) {
    if (sample.patch.size() != QSize(prefixWidth, prefixHeight))
      continue;
    for (const auto &tag : tags)
      if (initialIdentifier(QString::fromStdString(sample.tag)) ==
          initialIdentifier(QString::fromStdString(tag))) {
        patterns.push_back({tag, sample.patch.convertToFormat(QImage::Format_Grayscale8)});
        break;
      }
  }
  result.standings.clear();
  result.evidence.clear();
  for (int row = 0; row < 12; ++row) {
    const auto gray = result.image.copy(1014, 87 + 78 * row, prefixWidth + 8, prefixHeight + 6)
                          .convertToFormat(QImage::Format_Grayscale8);
    std::map<std::string, std::pair<double, double>> scores;
    for (const auto &pattern : patterns) {
      auto &score = scores[pattern.tag];
      score.first = std::max(score.first, match(gray, pattern.gray, initialWidth));
      score.second = std::max(score.second, match(gray, pattern.gray, prefixWidth));
    }
    std::string firstTag, prefixTag;
    double first = 0, nextFirst = 0, prefix = 0, nextPrefix = 0;
    for (const auto &[tag, score] : scores) {
      if (score.first > first) {
        nextFirst = first;
        first = score.first;
        firstTag = tag;
      } else
        nextFirst = std::max(nextFirst, score.first);
      if (score.second > prefix) {
        nextPrefix = prefix;
        prefix = score.second;
        prefixTag = tag;
      } else
        nextPrefix = std::max(nextPrefix, score.second);
    }
    // Both checks must agree. Team capacity validates the answer; it cannot fill it.
    const bool accepted = !firstTag.empty() && firstTag == prefixTag && first >= .86 &&
                          first - nextFirst >= .08 && prefix >= .88 && prefix - nextPrefix >= .10;
    result.standings.push_back(
        {row + 1, accepted ? firstTag : std::string{}, accepted ? std::min(first, prefix) : 0});
    result.evidence.push_back(
        {{}, QStringLiteral("画像照合 / 先頭文字と名前の見本"), prefix, prefixTag, prefix});
  }
  return result;
}
} // namespace quickscore
