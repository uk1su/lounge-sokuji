#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace quickscore {

struct Frame {
  int width{};
  int height{};
  int stride{};
  std::vector<uint8_t> rgba;
};

struct NormalizedRect {
  double x{};
  double y{};
  double width{};
  double height{};
};

std::vector<uint8_t> normalizedPatch(const Frame &frame, NormalizedRect rect,
                                     int outputWidth, int outputHeight,
                                     bool threshold = true);
double normalizedCorrelation(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b);

class ResultDetector {
public:
  enum class Event { None, ResultAppeared, ResultDisappeared };
  void setTemplate(std::vector<uint8_t> image);
  Event observe(const std::vector<uint8_t> &image, double threshold = 0.80);
  bool calibrated() const { return !template_.empty(); }
  bool visible() const { return visible_; }

private:
  std::vector<uint8_t> template_;
  int positiveFrames_{};
  int negativeFrames_{};
  bool visible_{};
};

class TagRecognizer {
public:
  void learn(const std::string &tag, std::vector<uint8_t> image);
  std::pair<std::string, double> classify(const std::vector<uint8_t> &image) const;
  void clear();
  size_t sampleCount() const;
  bool save(const std::string &path) const;
  bool load(const std::string &path);

private:
  std::map<std::string, std::vector<std::vector<uint8_t>>> samples_;
};

} // namespace quickscore
