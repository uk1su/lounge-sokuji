#include "vision.hpp"

#include <cassert>
#include <iostream>

int main()
{
  using quickscore::ResultDetector;
  const std::vector<uint8_t> a{0, 0, 255, 255};
  const std::vector<uint8_t> b{0, 0, 255, 255};
  const std::vector<uint8_t> c{255, 255, 0, 0};
  assert(quickscore::normalizedCorrelation(a, b) > .99);
  assert(quickscore::normalizedCorrelation(a, c) < -.99);
  ResultDetector detector;
  detector.setTemplate(a);
  assert(detector.observe(b) == ResultDetector::Event::None);
  assert(detector.observe(b) == ResultDetector::Event::None);
  assert(detector.observe(b) == ResultDetector::Event::ResultAppeared);
  assert(detector.observe(c) == ResultDetector::Event::None);
  assert(detector.observe(c) == ResultDetector::Event::None);
  assert(detector.observe(c) == ResultDetector::Event::ResultDisappeared);
  quickscore::TagRecognizer recognizer;
  recognizer.learn("A", a);
  recognizer.learn("B", c);
  assert(recognizer.classify(b).first == "A");
  assert(recognizer.classify(c).first == "B");
  std::cout << "quickscore vision tests passed\n";
}
