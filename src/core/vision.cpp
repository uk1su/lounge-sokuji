#include "vision.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace quickscore {

std::vector<uint8_t> normalizedPatch(const Frame &frame, NormalizedRect rect,
                                     int outputWidth, int outputHeight, bool threshold)
{
  if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
      frame.rgba.size() < static_cast<size_t>(frame.stride * frame.height) ||
      outputWidth <= 0 || outputHeight <= 0)
    return {};

  const int x0 = std::max(0, std::min(frame.width - 1, static_cast<int>(rect.x * frame.width)));
  const int y0 = std::max(0, std::min(frame.height - 1, static_cast<int>(rect.y * frame.height)));
  const int cropW = std::max(1, std::min(frame.width - x0, static_cast<int>(rect.width * frame.width)));
  const int cropH = std::max(1, std::min(frame.height - y0, static_cast<int>(rect.height * frame.height)));
  std::vector<uint8_t> result(static_cast<size_t>(outputWidth * outputHeight));
  uint64_t sum = 0;
  for (int y = 0; y < outputHeight; ++y) {
    const int sy = y0 + (y * cropH) / outputHeight;
    for (int x = 0; x < outputWidth; ++x) {
      const int sx = x0 + (x * cropW) / outputWidth;
      const uint8_t *pixel = frame.rgba.data() + sy * frame.stride + sx * 4;
      const uint8_t gray = static_cast<uint8_t>((77 * pixel[0] + 150 * pixel[1] + 29 * pixel[2]) >> 8);
      result[static_cast<size_t>(y * outputWidth + x)] = gray;
      sum += gray;
    }
  }
  if (threshold) {
    const uint8_t mean = static_cast<uint8_t>(sum / result.size());
    for (auto &value : result) value = value >= mean ? 255 : 0;
  }
  return result;
}

double normalizedCorrelation(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
  if (a.empty() || a.size() != b.size()) return -1.0;
  double meanA = 0.0, meanB = 0.0;
  for (size_t i = 0; i < a.size(); ++i) { meanA += a[i]; meanB += b[i]; }
  meanA /= a.size(); meanB /= b.size();
  double numerator = 0.0, normA = 0.0, normB = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    const double da = a[i] - meanA;
    const double db = b[i] - meanB;
    numerator += da * db;
    normA += da * da;
    normB += db * db;
  }
  const double denominator = std::sqrt(normA * normB);
  return denominator > 0.0 ? numerator / denominator : (a == b ? 1.0 : -1.0);
}

void ResultDetector::setTemplate(std::vector<uint8_t> image)
{
  template_ = std::move(image);
  positiveFrames_ = negativeFrames_ = 0;
  visible_ = false;
}

ResultDetector::Event ResultDetector::observe(const std::vector<uint8_t> &image, double threshold)
{
  if (template_.empty()) return Event::None;
  const bool matched = normalizedCorrelation(template_, image) >= threshold;
  if (matched) { ++positiveFrames_; negativeFrames_ = 0; }
  else { ++negativeFrames_; positiveFrames_ = 0; }
  if (!visible_ && positiveFrames_ >= 3) { visible_ = true; return Event::ResultAppeared; }
  if (visible_ && negativeFrames_ >= 3) { visible_ = false; return Event::ResultDisappeared; }
  return Event::None;
}

void TagRecognizer::learn(const std::string &tag, std::vector<uint8_t> image)
{
  if (tag.empty() || image.empty()) return;
  auto &items = samples_[tag];
  items.push_back(std::move(image));
  if (items.size() > 24) items.erase(items.begin());
}

std::pair<std::string, double> TagRecognizer::classify(const std::vector<uint8_t> &image) const
{
  std::string bestTag;
  double best = -1.0;
  for (const auto &group : samples_)
    for (const auto &sample : group.second) {
      const double score = normalizedCorrelation(sample, image);
      if (score > best) { best = score; bestTag = group.first; }
    }
  return {bestTag, std::max(0.0, std::min(1.0, (best + 1.0) * 0.5))};
}

void TagRecognizer::clear() { samples_.clear(); }
size_t TagRecognizer::sampleCount() const
{
  size_t count = 0;
  for (const auto &group : samples_) count += group.second.size();
  return count;
}

bool TagRecognizer::save(const std::string &path) const
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  const uint32_t magic = 0x51535431;
  const uint32_t groups = static_cast<uint32_t>(samples_.size());
  out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
  out.write(reinterpret_cast<const char *>(&groups), sizeof(groups));
  for (const auto &group : samples_) {
    const uint32_t tagSize = static_cast<uint32_t>(group.first.size());
    const uint32_t count = static_cast<uint32_t>(group.second.size());
    out.write(reinterpret_cast<const char *>(&tagSize), sizeof(tagSize));
    out.write(group.first.data(), tagSize);
    out.write(reinterpret_cast<const char *>(&count), sizeof(count));
    for (const auto &sample : group.second) {
      const uint32_t size = static_cast<uint32_t>(sample.size());
      out.write(reinterpret_cast<const char *>(&size), sizeof(size));
      out.write(reinterpret_cast<const char *>(sample.data()), size);
    }
  }
  return static_cast<bool>(out);
}

bool TagRecognizer::load(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  uint32_t magic = 0, groups = 0;
  in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
  in.read(reinterpret_cast<char *>(&groups), sizeof(groups));
  if (!in || magic != 0x51535431 || groups > 12) return false;
  decltype(samples_) loaded;
  for (uint32_t g = 0; g < groups; ++g) {
    uint32_t tagSize = 0, count = 0;
    in.read(reinterpret_cast<char *>(&tagSize), sizeof(tagSize));
    if (!in || tagSize == 0 || tagSize > 64) return false;
    std::string tag(tagSize, '\0');
    in.read(&tag[0], tagSize);
    in.read(reinterpret_cast<char *>(&count), sizeof(count));
    if (!in || count > 24) return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t size = 0;
      in.read(reinterpret_cast<char *>(&size), sizeof(size));
      if (!in || size == 0 || size > 65536) return false;
      std::vector<uint8_t> sample(size);
      in.read(reinterpret_cast<char *>(sample.data()), size);
      if (!in) return false;
      loaded[tag].push_back(std::move(sample));
    }
  }
  samples_ = std::move(loaded);
  return true;
}

} // namespace quickscore
