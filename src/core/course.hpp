#pragma once

#include <QImage>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quickscore {

const std::vector<std::string> &canonicalCourseNames();

class CourseRecognizer {
public:
  CourseRecognizer();
  ~CourseRecognizer();

  static bool isCourseScreen(const QImage &source);
  static std::optional<std::string> matchCourseName(const std::string &rawOcrText);
  std::optional<std::string> recognize(const QImage &source);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class CourseGate {
public:
  std::optional<std::string> observe(const std::optional<std::string> &detectedCourse,
                                     uint64_t nowMs, bool allow = true);
  void reset();
  bool locked() const { return locked_; }
  const std::string &lastCourse() const { return candidate_; }

private:
  bool locked_{false};
  std::string candidate_;
  std::string sentCourse_;
  int stableCount_{0};
  int missCount_{0};
  uint64_t lastTimeMs_{0};
};

} // namespace quickscore
