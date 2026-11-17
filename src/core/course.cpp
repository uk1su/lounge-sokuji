#include "course.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <MemoryBuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#endif

namespace quickscore {
namespace {

struct CourseEntry {
  const char *canonical;
  const char *inGame;
};

constexpr CourseEntry kCourseDefinitions[] = {
    {"マリオカートスタジアム", "マリオカートスタジアム"},
    {"ウォーターパーク", "ウォーターパーク"},
    {"スイーツキャニオン", "スイーツキャニオン"},
    {"ドッスンいせき", "ドッスンいせき"},
    {"マリオサーキット", "マリオサーキット"},
    {"キノピオハーバー", "キノピオハーバー"},
    {"ねじれマンション", "ねじれマンション"},
    {"ヘイホーこうざん", "ヘイホーこうざん"},
    {"サンシャインくうこう", "サンシャインくうこう"},
    {"ドルフィンみさき", "ドルフィンみさき"},
    {"エレクトロドリーム", "エレクトロドリーム"},
    {"ワリオスノーマウンテン", "ワリオスノーマウンテン"},
    {"スカイガーデン", "スカイガーデン"},
    {"ホネホネさばく", "ホネホネさばく"},
    {"クッパキャッスル", "クッパキャッスル"},
    {"レインボーロード", "レインボーロード"},
    {"モーモーカントリー", "Wiiモーモーカントリー"},
    {"GBAマリオサーキット", "GBAマリオサーキット"},
    {"プクプクビーチ", "DSプクプクビーチ"},
    {"キノピオハイウェイ", "N64キノピオハイウェイ"},
    {"カラカラさばく", "GCカラカラさばく"},
    {"ドーナツへいや3", "SFCドーナツへいや3"},
    {"ピーチサーキット", "N64ピーチサーキット"},
    {"DKジャングル", "3DSDKジャングル"},
    {"ワリオスタジアム", "DSワリオスタジアム"},
    {"シャーベットランド", "GCシャーベットランド"},
    {"ミュージックパーク", "3DSミュージックパーク"},
    {"ヨッシーバレー", "N64ヨッシーバレー"},
    {"チクタクロック", "DSチクタクロック"},
    {"パックンスライダー", "3DSパックンスライダー"},
    {"グラグラかざん", "Wiiグラグラかざん"},
    {"64レインボーロード", "N64レインボーロード"},
    {"ヨッシーサーキット", "GCヨッシーサーキット"},
    {"エキサイトバイク", "エキサイトバイク"},
    {"ドラゴンロード", "ドラゴンロード"},
    {"ミュートシティ", "ミュートシティ"},
    {"ワリオこうざん", "Wiiワリオこうざん"},
    {"SFCレインボーロード", "SFCレインボーロード"},
    {"ツルツルツイスター", "ツルツルツイスター"},
    {"ハイラルサーキット", "ハイラルサーキット"},
    {"ベビィパーク", "GCベビィパーク"},
    {"チーズランド", "GBAチーズランド"},
    {"ネイチャーロード", "ネイチャーロード"},
    {"どうぶつの森", "どうぶつの森"},
    {"ネオクッパシティ", "3DSネオクッパシティ"},
    {"リボンロード", "GBAリボンロード"},
    {"リンリンメトロ", "リンリンメトロ"},
    {"ビッグブルー", "ビッグブルー"},
    {"パリプロムナード", "Tourパリプロムナード"},
    {"キノピオサーキット", "3DSキノピオサーキット"},
    {"チョコマウンテン", "N64チョコマウンテン"},
    {"ココナッツモール", "Wiiココナッツモール"},
    {"トーキョースクランブル", "Tourトーキョースクランブル"},
    {"キノコリッジウェイ", "DSキノコリッジウェイ"},
    {"GBAスカイガーデン", "GBAスカイガーデン"},
    {"ニンニンドージョー", "Tourニンニンドージョー"},
    {"ニューヨークドリーム", "Tourニューヨークドリーム"},
    {"SFCマリオサーキット3", "SFCマリオサーキット3"},
    {"64カラカラさばく", "N64カラカラさばく"},
    {"ワルイージピンボール", "DSワルイージピンボール"},
    {"シドニーサンシャイン", "Tourシドニーサンシャイン"},
    {"スノーランド", "GBAスノーランド"},
    {"キノコキャニオン", "Wiiキノコキャニオン"},
    {"アイスビルディング", "アイスビルディング"},
    {"ロンドンアベニュー", "Tourロンドンアベニュー"},
    {"テレサレイク", "GBAテレサレイク"},
    {"ロックロックマウンテン", "3DSロックロックマウンテン"},
    {"メイプルツリーハウス", "Wiiメイプルツリーハウス"},
    {"ベルリンシュトラーセ", "Tourベルリンシュトラーセ"},
    {"ピーチガーデン", "DSピーチガーデン"},
    {"メリーメリーマウンテン", "Tourメリーメリーマウンテン"},
    {"3DSレインボーロード", "3DSレインボーロード"},
    {"アムステルダムブルーム", "Tourアムステルダムブルーム"},
    {"リバーサイドパーク", "GBAリバーサイドパーク"},
    {"DKスノーボードクロス", "WiiDKスノーボードクロス"},
    {"ヨッシーアイランド", "ヨッシーアイランド"},
    {"バンコクラッシュ", "Tourバンコクラッシュ"},
    {"DSマリオサーキット", "DSマリオサーキット"},
    {"ワルイージスタジアム", "GCワルイージスタジアム"},
    {"シンガポールスプラッシュ", "Tourシンガポールスプラッシュ"},
    {"アテネポリス", "Tourアテネポリス"},
    {"デイジークルーザー", "GCデイジークルーザー"},
    {"ムーンリッジ&ハイウェイ", "Wiiムーンリッジ&ハイウェイ"},
    {"シャボンロード", "シャボンロード"},
    {"ロサンゼルスコースト", "Tourロサンゼルスコースト"},
    {"サンセットこうや", "GBAサンセットこうや"},
    {"ノコノコみさき", "Wiiノコノコみさき"},
    {"バンクーバーバレー", "Tourバンクーバーバレー"},
    {"ローマアバンティ", "Tourローマアバンティ"},
    {"DKマウンテン", "GCDKマウンテン"},
    {"デイジーサーキット", "Wiiデイジーサーキット"},
    {"パックンしんでん", "Tourパックンしんでん"},
    {"マドリードグランデ", "Tourマドリードグランデ"},
    {"ロゼッタプラネット", "3DSロゼッタプラネット"},
    {"クッパじょう3", "SFCクッパじょう3"},
    {"Wiiレインボーロード", "Wiiレインボーロード"},
};

QImage ensureHdArgb32(const QImage &source) {
  if (source.isNull())
    return {};
  QImage img = source;
  if (img.width() != 1920 || img.height() != 1080)
    img = img.scaled(1920, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied &&
      img.format() != QImage::Format_RGB32) {
    img = img.convertToFormat(QImage::Format_ARGB32);
  }
  return img;
}

std::string normalizeText(const std::string &str) {
  std::string out;
  out.reserve(str.size());
  for (size_t i = 0; i < str.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(str[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      continue;
    if (c == 0xE3 && i + 2 < str.size() && static_cast<unsigned char>(str[i + 1]) == 0x80 &&
        static_cast<unsigned char>(str[i + 2]) == 0x80) {
      i += 2;
      continue;
    }
    out.push_back(str[i]);
  }
  return out;
}

std::u32string toNormalizedCodepoints(const std::string &text) {
  std::u32string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i++]);
    char32_t cp = c;
    int remaining = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      cp = c & 0x1f;
      remaining = 1;
    } else if (c >= 0xe0 && c <= 0xef) {
      cp = c & 0x0f;
      remaining = 2;
    } else if (c >= 0xf0 && c <= 0xf4) {
      cp = c & 0x07;
      remaining = 3;
    } else if (c >= 0x80) {
      return {};
    }
    if (i + static_cast<size_t>(remaining) > text.size())
      return {};
    for (int k = 0; k < remaining; ++k) {
      const auto b = static_cast<unsigned char>(text[i++]);
      if ((b & 0xc0) != 0x80)
        return {};
      cp = (cp << 6) | (b & 0x3f);
    }
    if ((remaining == 1 && cp < 0x80) || (remaining == 2 && cp < 0x800) ||
        (remaining == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return {};
    if (cp == U' ' || cp == U'\t' || cp == U'\r' || cp == U'\n' || cp == 0x3000)
      continue;
    if (cp >= 0xff01 && cp <= 0xff5e)
      cp -= 0xfee0;
    if (cp >= U'a' && cp <= U'z')
      cp -= 32;
    constexpr std::u32string_view kSmallKana = U"ァィゥェォッャュョヮヵヶ";
    constexpr std::u32string_view kLargeKana = U"アイウエオツヤユヨワカケ";
    const auto pos = kSmallKana.find(cp);
    if (pos != std::u32string_view::npos)
      cp = kLargeKana[pos];
    out.push_back(cp);
  }
  return out;
}

struct CourseTitle {
  std::u32string prefix;
  std::u32string body;
};

CourseTitle splitTitle(const std::string &text) {
  const auto clean = toNormalizedCodepoints(text);
  for (const auto *prefix :
       {U"3DS", U"N64", U"64", U"GBA", U"WII", U"SFC", U"5FC", U"SEC", U"GC", U"DS", U"TOUR"}) {
    const std::u32string p = prefix;
    if (clean.rfind(p, 0) == 0) {
      std::u32string norm = p;
      if (p == U"64")
        norm = U"N64";
      else if (p == U"5FC" || p == U"SEC")
        norm = U"SFC";
      return {norm, clean.substr(p.size())};
    }
  }
  return {{}, clean};
}

char32_t unvoicedKana(char32_t c) {
  constexpr std::u32string_view marked = U"ガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポヴ";
  constexpr std::u32string_view plain = U"カキクケコサシスセソタチツテトハヒフヘホハヒフヘホウ";
  const auto pos = marked.find(c);
  return pos == std::u32string_view::npos ? c : plain[pos];
}

int orderedEditScore(const std::u32string &a, const std::u32string &b) {
  if (a.size() < 4 || b.empty() ||
      std::min(a.size(), b.size()) * 100 < std::max(a.size(), b.size()) * 65)
    return 0;
  std::vector<double> previous(b.size() + 1), current(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j)
    previous[j] = static_cast<double>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    current[0] = static_cast<double>(i);
    for (size_t j = 1; j <= b.size(); ++j) {
      const double substitution =
          a[i - 1] == b[j - 1] ? 0.0 : (unvoicedKana(a[i - 1]) == unvoicedKana(b[j - 1]) ? 0.25 : 1.0);
      current[j] = std::min({previous[j] + 1.0, current[j - 1] + 1.0, previous[j - 1] + substitution});
    }
    previous.swap(current);
  }
  return static_cast<int>(
      std::lround(10000.0 * (1.0 - previous.back() / static_cast<double>(std::max(a.size(), b.size())))));
}

int scoreEntry(const std::string &ocrClean, const CourseEntry &entry) {
  const auto input = splitTitle(ocrClean);
  const auto game = splitTitle(entry.inGame);
  const auto canonical = splitTitle(entry.canonical);
  if (!input.prefix.empty() && input.prefix != game.prefix)
    return 0;
  const bool gba = input.prefix == U"GBA";
  if (std::strcmp(entry.canonical, "GBAスカイガーデン") == 0 && !gba)
    return 0;
  if (std::strcmp(entry.canonical, "スカイガーデン") == 0 && gba)
    return 0;
  int best = std::max(orderedEditScore(input.body, game.body),
                      orderedEditScore(input.body, canonical.body));
  // Exception: "SFCマリオサーキット3" has a unique trailing '3' in its body,
  // and Windows OCR frequently drops the far-left "SFC" badge.
  if (input.prefix.empty() && !canonical.prefix.empty() &&
      std::strcmp(entry.canonical, "SFCマリオサーキット3") != 0)
    best = std::max(0, best - 1200);
  return best;
}

} // namespace

struct CourseRecognizer::Impl {
#ifdef _WIN32
  winrt::Windows::Media::Ocr::OcrEngine engine{nullptr};
  bool engineAttempted{false};
  winrt::Windows::Media::Ocr::OcrEngine &ensureEngine() {
    static thread_local const bool init = [] {
      try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
      } catch (...) {
      }
      return true;
    }();
    (void)init;
    if (!engineAttempted) {
      engineAttempted = true;
      try {
        winrt::Windows::Globalization::Language lang(L"ja");
        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(lang);
        if (!engine)
          engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
      } catch (...) {
      }
    }
    return engine;
  }
#endif
};

CourseRecognizer::CourseRecognizer() : impl_(std::make_unique<Impl>()) {}
CourseRecognizer::~CourseRecognizer() = default;

bool CourseRecognizer::isCourseScreen(const QImage &source) {
  if (source.isNull() || source.width() <= 0 || source.height() <= 0)
    return false;

  auto samplePixel = [&](int x1920, int y1080) -> QRgb {
    const int sx = std::clamp(static_cast<int>(std::lround(x1920 * double(source.width()) / 1920.0)),
                              0, source.width() - 1);
    const int sy = std::clamp(static_cast<int>(std::lround(y1080 * double(source.height()) / 1080.0)),
                              0, source.height() - 1);
    return source.pixel(sx, sy);
  };

  for (int x : {600, 960, 1300}) {
    const QRgb top = samplePixel(x, 900);
    if (qRed(top) > 45 || qGreen(top) > 45 || qBlue(top) > 45)
      return false;
    const QRgb bot = samplePixel(x, 1060);
    if (qRed(bot) > 45 || qGreen(bot) > 45 || qBlue(bot) > 45)
      return false;
  }

  int grayCount = 0;
  for (int y : {950, 1030}) {
    for (int x : {600, 750, 900, 1050, 1200}) {
      const QRgb p = samplePixel(x, y);
      const int r = qRed(p), g = qGreen(p), b = qBlue(p);
      const int minv = std::min({r, g, b});
      const int maxv = std::max({r, g, b});
      if (minv >= 130 && maxv <= 210 && (maxv - minv) <= 18)
        ++grayCount;
    }
  }
  return grayCount >= 8;
}

std::optional<std::string> CourseRecognizer::matchCourseName(const std::string &rawOcrText) {
  const std::string clean = normalizeText(rawOcrText);
  if (clean.empty())
    return std::nullopt;

  int bestScore = 0;
  int secondScore = 0;
  const char *bestCourse = nullptr;
  for (const auto &entry : kCourseDefinitions) {
    const int sc = scoreEntry(clean, entry);
    if (sc > bestScore) {
      secondScore = bestScore;
      bestScore = sc;
      bestCourse = entry.canonical;
    } else {
      secondScore = std::max(secondScore, sc);
    }
  }
  if (bestScore >= 7800 && bestScore - secondScore >= 800 && bestCourse)
    return std::string(bestCourse);
  return std::nullopt;
}

std::optional<std::string> CourseRecognizer::recognize(const QImage &source) {
#ifdef _WIN32
  if (!impl_ || !impl_->ensureEngine())
    return std::nullopt;

  const QImage im = ensureHdArgb32(source);
  if (im.isNull())
    return std::nullopt;

  auto runOcr = [&](const QImage &area) -> std::string {
    const QImage premul = area.format() == QImage::Format_ARGB32_Premultiplied
                              ? area
                              : area.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    std::string rawText;
    try {
      using namespace winrt::Windows::Graphics::Imaging;
      SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, premul.width(), premul.height(),
                            BitmapAlphaMode::Premultiplied);
      {
        auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
        auto reference = buffer.CreateReference();
        uint8_t *bytes = nullptr;
        uint32_t capacity = 0;
        reference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(&bytes, &capacity);
        const auto plane = buffer.GetPlaneDescription(0);
        for (int y = 0; y < premul.height(); ++y) {
          std::memcpy(bytes + plane.StartIndex + y * plane.Stride, premul.constScanLine(y),
                      size_t(premul.width() * 4));
        }
      }
      rawText = winrt::to_string(impl_->engine.RecognizeAsync(bitmap).get().Text());
    } catch (...) {
    }
    return rawText;
  };

  QImage titleArea = im.copy(450, 945, 1000, 90);
  if (auto matched = matchCourseName(runOcr(titleArea)))
    return matched;

  // Fallback for noisy/low-contrast banners: trim cup icon & right scene, upscale 2x,
  // and stretch dark low-chroma glyphs onto clean white for WinRT OCR.
  QImage cleanStrip = im.copy(470, 950, 930, 80)
                          .scaled(1860, 160, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < cleanStrip.height(); ++y) {
    QRgb *line = reinterpret_cast<QRgb *>(cleanStrip.scanLine(y));
    for (int x = 0; x < cleanStrip.width(); ++x) {
      const QRgb p = line[x];
      const int r = qRed(p), g = qGreen(p), b = qBlue(p);
      const int minv = std::min({r, g, b});
      const int maxv = std::max({r, g, b});
      int v = 255;
      if (maxv - minv <= 45) {
        const int luma = (r + g + b) / 3;
        v = std::clamp((luma - 55) * 255 / 60, 0, 255);
      }
      line[x] = qRgba(v, v, v, 255);
    }
  }
  return matchCourseName(runOcr(cleanStrip));
#else
  (void)source;
  return std::nullopt;
#endif
}

std::optional<std::string> CourseGate::observe(const std::optional<std::string> &detectedCourse,
                                               uint64_t nowMs, bool allow) {
  if (lastTimeMs_ && (nowMs < lastTimeMs_ || nowMs - lastTimeMs_ > 1500)) {
    stableCount_ = 0;
    missCount_ = 0;
    candidate_.clear();
  }
  lastTimeMs_ = nowMs;

  if (!detectedCourse.has_value()) {
    // Allow one transient OCR miss without dropping a valid first frame.
    if (++missCount_ >= 2) {
      stableCount_ = 0;
      candidate_.clear();
    }
    return std::nullopt;
  }
  missCount_ = 0;

  const std::string &course = *detectedCourse;
  if (candidate_ != course) {
    candidate_ = course;
    stableCount_ = 1;
    return std::nullopt;
  }

  ++stableCount_;
  if (allow && stableCount_ >= 2 && (!locked_ || course != sentCourse_)) {
    locked_ = true;
    sentCourse_ = course;
    return course;
  }
  return std::nullopt;
}

void CourseGate::reset() {
  locked_ = false;
  candidate_.clear();
  sentCourse_.clear();
  stableCount_ = 0;
  missCount_ = 0;
  lastTimeMs_ = 0;
}

const std::vector<std::string> &canonicalCourseNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> out;
    out.reserve(std::size(kCourseDefinitions));
    for (const auto &entry : kCourseDefinitions)
      out.emplace_back(entry.canonical);
    return out;
  }();
  return names;
}

} // namespace quickscore
