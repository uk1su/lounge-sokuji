#include "native-ocr.hpp"
#include "vision.hpp"

#include <QImage>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>

#include <algorithm>
#include <iostream>

int main(int argc, char **argv)
{
  if (argc != 2) return 2;
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  std::cout << "OCR languages:";
  for (const auto &language : winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages())
    std::cout << ' ' << winrt::to_string(language.LanguageTag());
  std::cout << '\n';
  QImage image(QString::fromLocal8Bit(argv[1]));
  if (image.isNull()) return 3;
  image = image.convertToFormat(QImage::Format_RGBA8888);
  quickscore::Frame frame;
  frame.width = image.width();
  frame.height = image.height();
  frame.stride = image.bytesPerLine();
  frame.rgba.resize(static_cast<size_t>(frame.stride * frame.height));
  std::copy(image.constBits(), image.constBits() + frame.rgba.size(), frame.rgba.begin());

  constexpr double x = .4300;
  constexpr double top = .0694;
  constexpr double width = .3600;
  constexpr double height = .0650;
  constexpr double step = .0722;
  const std::vector<std::string> tags{"かない", "じ", "A", "I"};
  for (int row = 0; row < 12; ++row) {
    const auto patch = quickscore::normalizedPatch(frame, {x, top + row * step, width, height}, 320, 48, false);
    const auto text = quickscore::recognizeNativeText(patch, 320, 48);
    std::cout << row + 1 << ": " << text << " => " << quickscore::matchKnownTag(text, tags) << '\n';
  }
  const auto panel = quickscore::normalizedPatch(frame, {.50, .055, .30, .88}, 500, 780, false);
  std::cout << "PANEL: " << quickscore::recognizeNativeText(panel, 500, 780) << '\n';
}
