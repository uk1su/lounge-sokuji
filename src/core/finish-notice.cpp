#include "finish-notice.hpp"
#include <string_view>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Globalization;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Ocr;
using namespace Windows::Storage::Streams;
#endif

namespace quickscore {
namespace {
std::string without_whitespace(std::string_view text) {
  std::string result;
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    // U+3000 IDEOGRAPHIC SPACE, encoded as E3 80 80.
    if (c == 0xe3 && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) == 0x80 &&
        static_cast<unsigned char>(text[i + 2]) == 0x80) { i += 2; continue; }
    result.push_back(static_cast<char>(c));
  }
  return result;
}
} // namespace

bool isTwelfthFinishNoticeText(const std::string& text) {
  if (text.empty()) return false;
  if (text == twelfthFinishNoticeText) return true;
  const std::string cleanText = without_whitespace(text);
  const std::string cleanTarget = without_whitespace(twelfthFinishNoticeText);
  if (cleanText == cleanTarget) return true;
  if (cleanText.find(cleanTarget) != std::string::npos) return true;
  // OCRの部分一致（「順位が確定」や「レースを終了しました」など）
  if (cleanText.find("順位が確定") != std::string::npos ||
      cleanText.find("レースを終了しました") != std::string::npos ||
      cleanText.find("順位が確定したので") != std::string::npos) {
    return true;
  }
  return false;
}

bool hasTwelfthFinishNotice(const QImage &source) {
  if (source.isNull()) return false;
  QImage norm = source;
  if (norm.width() != 1920 || norm.height() != 1080) {
    norm = norm.scaled(1920, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  // バナー領域: 350, 965, 1220, 115
  const QImage noticeArea = norm.copy(350, 965, 1220, 115).convertToFormat(QImage::Format_ARGB32_Premultiplied);
  std::string text;
#ifdef _WIN32
  try {
    static thread_local const bool initialized = [] {
      try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
      } catch (...) {}
      return true;
    }();
    (void)initialized;

    static thread_local OcrEngine engine = [] {
      try {
        auto ja = OcrEngine::TryCreateFromLanguage(Language(L"ja"));
        if (ja) return ja;
        return OcrEngine::TryCreateFromUserProfileLanguages();
      } catch (...) {
        return OcrEngine{nullptr};
      }
    }();
    if (!engine) return false;

    // QImage (ARGB32) -> SoftwareBitmap (Bgra8)
    const int w = noticeArea.width();
    const int h = noticeArea.height();
    DataWriter writer;
    const uint8_t *bits = noticeArea.constBits();
    const int bytesPerLine = noticeArea.bytesPerLine();
    if (bytesPerLine == w * 4) {
      writer.WriteBytes(array_view<const uint8_t>(bits, bits + (w * h * 4)));
    } else {
      std::vector<uint8_t> contiguous(size_t(w * h * 4));
      for (int y = 0; y < h; ++y) {
        std::copy_n(noticeArea.constScanLine(y), w * 4, contiguous.data() + (y * w * 4));
      }
      writer.WriteBytes(array_view<const uint8_t>(contiguous.data(), contiguous.data() + contiguous.size()));
    }
    const auto buffer = writer.DetachBuffer();
    const auto bitmap = SoftwareBitmap::CreateCopyFromBuffer(
        buffer, BitmapPixelFormat::Bgra8, w, h, BitmapAlphaMode::Premultiplied);
    text = winrt::to_string(engine.RecognizeAsync(bitmap).get().Text());
  } catch (...) {
    return false;
  }
#endif
  return isTwelfthFinishNoticeText(text);
}

} // namespace quickscore
