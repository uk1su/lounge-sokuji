#pragma once
#include <cstdint>
#include <string>

namespace quickscore {
// Screen presence is determined by rank anchors, never by OCR success.
class ResultGate {
public:
  bool observe(bool visible,
               bool complete,
               const std::string &candidate,
               uint64_t now,
               bool immediate = false) {
    if (last_ && (now < last_ || now - last_ > 5000))
      clearCandidate();
    const bool continuous = last_ && now >= last_ && now - last_ <= 1000;
    last_ = now;
    if (!visible) {
      clearCandidate();
      if (!continuous || !absent_)
        absent_ = now;
      if (now - absent_ >= 3000) {
        if (!armed_ || locked_)
          ++cycle_;
        armed_ = true;
        locked_ = false;
      }
      return false;
    }
    absent_ = 0;
    if (!armed_ || locked_ || !complete || candidate.empty()) {
      clearCandidate();
      return false;
    }
    if (immediate) {
      locked_ = true;
      clearCandidate();
      return true;
    }
    if (candidate != candidate_) {
      candidate_ = candidate;
      first_ = now;
      count_ = 1;
      return false;
    }
    return ++count_ >= 3 && now - first_ >= 250;
  }
  void lock() {
    locked_ = true;
    clearCandidate();
  }
  void pause() {
    armed_ = false;
    absent_ = last_ = 0;
    clearCandidate();
  }
  bool armed() const { return armed_ && !locked_; }
  bool locked() const { return locked_; }
  uint64_t cycle() const { return cycle_; }

private:
  void clearCandidate() {
    candidate_.clear();
    count_ = 0;
    first_ = 0;
  }
  bool armed_ = false, locked_ = false;
  uint64_t absent_ = 0, first_ = 0, last_ = 0;
  unsigned count_ = 0;
  uint64_t cycle_ = 0;
  std::string candidate_;
};
} // namespace quickscore
