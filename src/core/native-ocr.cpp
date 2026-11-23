#include "native-ocr.hpp"

#ifdef _WIN32
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string_view>

#include <MemoryBuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

namespace quickscore {
namespace {
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

std::string runOcr(const std::vector<uint8_t> &gray, int width, int height, bool invert)
{
  static thread_local const bool initialized = [] {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    return true;
  }();
  (void)initialized;
  const int scale = 3;
  const int padding = 20;
  const int outputWidth = width * scale + padding * 2;
  const int outputHeight = height * scale + padding * 2;
  SoftwareBitmap bitmap(BitmapPixelFormat::Gray8, outputWidth, outputHeight, BitmapAlphaMode::Ignore);
  {
    auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
    auto reference = buffer.CreateReference();
    uint8_t *bytes = nullptr;
    uint32_t capacity = 0;
    reference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(&bytes, &capacity);
    const auto plane = buffer.GetPlaneDescription(0);
    const uint8_t background = invert ? 0 : 255;
    std::fill(bytes, bytes + capacity, background);
    for (int y = 0; y < height; ++y)
      for (int sy = 0; sy < scale; ++sy) {
        uint8_t *line = bytes + plane.StartIndex + (padding + y * scale + sy) * plane.Stride + padding;
        for (int x = 0; x < width; ++x) {
          const uint8_t value = invert ? static_cast<uint8_t>(255 - gray[static_cast<size_t>(y * width + x)])
                                       : gray[static_cast<size_t>(y * width + x)];
          std::fill(line + x * scale, line + (x + 1) * scale, value);
        }
      }
  }
  static thread_local OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
  if (!engine) return {};
  return winrt::to_string(engine.RecognizeAsync(bitmap).get().Text());
}

std::string compact(std::string value)
{
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
    return c <= 0x20 || c == '-' || c == '_' || c == '.' || c == ':';
  }), value.end());
  return value;
}

std::vector<uint32_t> codepoints(std::string_view value)
{
  std::vector<uint32_t> result;
  for (size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i]);
    const size_t count = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
    if (i + count > value.size()) break;
    uint32_t point = first & (count == 1 ? 0x7F : count == 2 ? 0x1F : count == 3 ? 0x0F : 0x07);
    for (size_t j = 1; j < count; ++j)
      point = (point << 6) | (static_cast<unsigned char>(value[i + j]) & 0x3F);
    result.push_back(point);
    i += count;
  }
  return result;
}
} // namespace

std::string recognizeNativeText(const std::vector<uint8_t> &gray, int width, int height)
{
  if (gray.size() != static_cast<size_t>(width * height)) return {};
  try {
    std::string normal = runOcr(gray, width, height, false);
    std::string inverted = runOcr(gray, width, height, true);
    return normal + "|" + inverted;
  } catch (const winrt::hresult_error &) {
    return {};
  }
}

std::string matchKnownTag(const std::string &recognized, const std::vector<std::string> &tags)
{
  if (recognized.empty()) return {};
  std::vector<std::string> longest = tags;
  std::sort(longest.begin(), longest.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
  size_t begin = 0;
  while (begin <= recognized.size()) {
    const size_t end = recognized.find('|', begin);
    std::string candidate = recognized.substr(begin, end == std::string::npos ? end : end - begin);
    while (!candidate.empty() && static_cast<unsigned char>(candidate.front()) < 0x80 &&
           !std::isalpha(static_cast<unsigned char>(candidate.front())))
      candidate.erase(candidate.begin());
    const auto candidatePoints = codepoints(candidate);
    for (const auto &tag : longest) {
      if (tag.empty()) continue;
      const auto tagPoints = codepoints(tag);
      if (candidate.rfind(tag, 0) == 0) return tag;
      for (size_t offset = 1; offset <= 3 && offset + tagPoints.size() <= candidatePoints.size(); ++offset)
        if (std::equal(tagPoints.begin(), tagPoints.end(), candidatePoints.begin() + static_cast<ptrdiff_t>(offset)))
          return tag;
      if (tagPoints.size() >= 3 && candidatePoints.size() >= tagPoints.size()) {
        size_t differences = 0;
        for (size_t i = 0; i < tagPoints.size(); ++i)
          differences += candidatePoints[i] != tagPoints[i];
        if (differences <= 1) return tag;
      }
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return {};
}

} // namespace quickscore
#elif defined(__APPLE__)
#include "mac-ocr.hpp"
namespace quickscore {
std::string recognizeNativeText(const std::vector<uint8_t> &gray, int width, int height) {
  if (gray.size() != static_cast<size_t>(width * height))
    return {};
  return macRecognizeGray(gray.data(), width, height);
}
std::string matchKnownTag(const std::string &, const std::vector<std::string> &) { return {}; }
} // namespace quickscore
#else
namespace quickscore {
std::string recognizeNativeText(const std::vector<uint8_t> &, int, int) { return {}; }
std::string matchKnownTag(const std::string &, const std::vector<std::string> &) { return {}; }
}
#endif
