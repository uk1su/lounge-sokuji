#include "analysis.hpp"
#include "finish-notice.hpp"
#include "native-ocr.hpp"
#include <QBuffer>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>
#include <set>

namespace quickscore {
namespace {
QString normalized(const QString &s) {
  QString out;
  for (const auto c : s.normalized(QString::NormalizationForm_KC).toCaseFolded())
    if (!c.isSpace())
      out += c;
  return out;
}
double similarity(const QImage &gray, const QImage &sample, int x, int y) {
  double a = 0, b = 0, aa = 0, bb = 0, ab = 0;
  int n = 0;
  for (int j = 0; j < sample.height(); j += 2)
    for (int i = 0; i < sample.width(); i += 2) {
      const double u = gray.constScanLine(y + j)[x + i], v = sample.constScanLine(j)[i];
      a += u;
      b += v;
      aa += u * u;
      bb += v * v;
      ab += u * v;
      ++n;
    }
  const double va = aa - a * a / n, vb = bb - b * b / n;
  return va > 100 && vb > 100 ? std::abs((ab - a * b / n) / std::sqrt(va * vb)) : 0;
}
} // namespace
QString initialIdentifier(const QString &s) {
  const QString trimmed = s.trimmed();
  if (trimmed.isEmpty())
    return {};
  // Select the displayed grapheme BEFORE normalization/case folding. Symbols are tags too.
  QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, trimmed);
  const int end = boundary.toNextBoundary();
  QString key = trimmed.left(end).normalized(QString::NormalizationForm_KC).toCaseFolded();
  for (auto &ch : key) {
    const ushort cp = ch.unicode();
    if (cp >= 0x3041 && cp <= 0x3096)
      ch = QChar(cp + 0x60);
  }
  return key.normalized(QString::NormalizationForm_C);
}
bool validateTeamTags(const std::vector<std::string> &tags, QString *error) {
  if (tags.empty()) {
    if (error)
      *error = QStringLiteral("チームタグが設定されていません");
    return false;
  }
  std::vector<QString> ids;
  ids.reserve(tags.size());
  for (size_t i = 0; i < tags.size(); ++i) {
    const QString tagStr = QString::fromStdString(tags[i]).trimmed();
    if (tagStr.isEmpty()) {
      if (error)
        *error = QStringLiteral("空のチームタグがあります");
      return false;
    }
    const QString id = initialIdentifier(tagStr);
    if (id.isEmpty()) {
      if (error)
        *error = QStringLiteral("チームタグ '%1' に有効な文字がありません").arg(tagStr);
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (ids[j] == id) {
        if (error)
          *error = QStringLiteral(
                       "チーム '%1' と '%2' の先頭文字識別子 '%3' が重複しています。"
                       "ラウンジ規定により対戦チーム間で先頭文字を重複させることはできません。")
                       .arg(QString::fromStdString(tags[j]), QString::fromStdString(tags[i]), id);
        return false;
      }
    }
    ids.push_back(id);
  }
  return true;
}
std::string exactTag(const std::string &text, const std::vector<std::string> &tags) {
  std::set<std::string> matches;
  for (const auto &part : QString::fromStdString(text).split('|')) {
    const QString candidate = part.trimmed();
    if (candidate.isEmpty())
      continue;
    const QString candidateId = initialIdentifier(candidate);
    if (candidateId.isEmpty())
      continue;

    std::string matchedTag;
    int matchCount = 0;
    for (const auto &tag : tags) {
      const QString tagStr = QString::fromStdString(tag).trimmed();
      if (tagStr.isEmpty())
        continue;
      if (initialIdentifier(tagStr) == candidateId) {
        matchedTag = tag;
        ++matchCount;
      }
    }
    if (matchCount == 1) {
      matches.insert(matchedTag);
    } else if (matchCount > 1) {
      // Fallback: If multiple tags share the same initial character, use longest exact prefix
      const QString candidateNorm = normalized(candidate);
      std::string best;
      int longest = 0;
      bool tied = false;
      for (const auto &tag : tags) {
        const auto key = normalized(QString::fromStdString(tag));
        if (key.isEmpty() || !candidateNorm.startsWith(key))
          continue;
        if (key.size() > longest) {
          best = tag;
          longest = int(key.size());
          tied = false;
        } else if (key.size() == longest && best != tag)
          tied = true;
      }
      if (!tied && !best.empty())
        matches.insert(best);
    }
  }
  return matches.size() == 1 ? *matches.begin() : std::string{};
}
Analyzer::Analyzer(const QString &templates)
    : reference_(mk::Recognizer::load(templates.toStdWString())) {}
QImage Analyzer::rowImage(const QImage &image, int row) {
  return image.copy(1005, 112 + 78 * row - 27, 425, 54);
}
std::pair<std::string, double> Analyzer::matchSamples(const QImage &row,
                                                      const std::vector<std::string> &tags,
                                                      const std::vector<TagSample> &samples) {
  const QImage gray = row.convertToFormat(QImage::Format_Grayscale8);
  std::map<std::string, double> scores;
  for (const auto &sample : samples) {
    std::string matchedTeam;
    for (const auto &tag : tags) {
      if (sample.tag == tag ||
          (!sample.tag.empty() && initialIdentifier(QString::fromStdString(sample.tag)) ==
                                      initialIdentifier(QString::fromStdString(tag)))) {
        matchedTeam = tag;
        break;
      }
    }
    if (matchedTeam.empty())
      continue;
    const QImage p = sample.patch.convertToFormat(QImage::Format_Grayscale8);
    if (p.width() < 8 || p.height() < 12 || p.width() > 240 || p.height() > 48)
      continue;
    double best = 0;
    // Prefix location may shift by a few pixels; never search the player-name suffix.
    for (int y = 0; y + p.height() <= gray.height(); ++y)
      for (int x = 0; x <= 50 && x + p.width() <= gray.width(); ++x)
        best = std::max(best, similarity(gray, p, x, y));
    scores[matchedTeam] = std::max(scores[matchedTeam], best);
  }
  std::string bestTag;
  double best = 0, second = 0;
  for (const auto &entry : scores) {
    if (entry.second > best) {
      second = best;
      best = entry.second;
      bestTag = entry.first;
    } else
      second = std::max(second, entry.second);
  }
  if (best < .84 || best - second < .08)
    return {{}, best};
  return {bestTag, best};
}
CharBox detectFirstCharBox(const QImage &rowCrop) {
  if (rowCrop.isNull())
    return {15, 36};
  const QImage gray = rowCrop.convertToFormat(QImage::Format_Grayscale8);
  const int h = std::min(40, gray.height() - 6);
  if (h <= 0)
    return {15, 36};

  int firstInk = -1;
  int lastInk = -1;
  int gap = 0;

  const int maxScanX = std::min(100, gray.width());
  for (int x = 10; x < maxScanX; ++x) {
    uint8_t minV = 255, maxV = 0;
    for (int y = 6; y < 6 + h; ++y) {
      const uint8_t v = gray.constScanLine(y)[x];
      minV = std::min(minV, v);
      maxV = std::max(maxV, v);
    }
    const bool ink = (maxV - minV >= 60);
    if (ink) {
      if (firstInk < 0)
        firstInk = x;
      lastInk = x;
      gap = 0;
      if (firstInk >= 0 && (lastInk - firstInk >= 36))
        break;
    } else if (firstInk >= 0) {
      ++gap;
      if (gap >= 4)
        break;
    }
  }

  if (firstInk < 0)
    return {15, 36};

  const int left = std::max(0, firstInk - 1);
  const int rawW = lastInk - firstInk + 1;
  const int width = std::clamp(rawW + 2, 20, 38);
  return {left, width};
}
double charSimilarity(const QString &c1, const QString &c2) {
  const QString id1 = initialIdentifier(c1);
  const QString id2 = initialIdentifier(c2);
  if (id1.isEmpty() || id2.isEmpty())
    return 0.0;
  if (id1 == id2)
    return 1.0;

  static const std::vector<std::pair<QString, QString>> similarPairs = {
      {QStringLiteral("0"), QStringLiteral("o")},
      {QStringLiteral("0"), QStringLiteral("d")},
      {QStringLiteral("0"), QStringLiteral("q")},
      {QStringLiteral("1"), QStringLiteral("l")},
      {QStringLiteral("1"), QStringLiteral("i")},
      {QStringLiteral("1"), QStringLiteral("|")},
      {QStringLiteral("i"), QStringLiteral("l")},
      {QStringLiteral("2"), QStringLiteral("z")},
      {QStringLiteral("8"), QStringLiteral("b")},
      {QStringLiteral("u"), QStringLiteral("v")},
      {QStringLiteral("u"), QStringLiteral("w")},
      {QStringLiteral("v"), QStringLiteral("w")},
      {QStringLiteral("c"), QStringLiteral("o")},
      {QStringLiteral("s"), QStringLiteral("5")},
      {QStringLiteral("ロ"), QStringLiteral("口")},
      {QStringLiteral("カ"), QStringLiteral("力")},
      {QStringLiteral("タ"), QStringLiteral("夕")},
      {QStringLiteral("ト"), QStringLiteral("卜")},
      {QStringLiteral("ソ"), QStringLiteral("ン")},
      {QStringLiteral("シ"), QStringLiteral("ツ")},
      {QStringLiteral("ウ"), QStringLiteral("ワ")}};

  for (const auto &p : similarPairs) {
    if ((id1 == p.first && id2 == p.second) || (id1 == p.second && id2 == p.first))
      return 0.72;
  }
  return 0.10;
}

QImage renderCharTemplate(const QString &character, int targetHeight) {
  if (character.isEmpty())
    return {};

  const int canvasW = 80;
  const int canvasH = 60;
  QImage img(canvasW, canvasH, QImage::Format_Grayscale8);
  img.fill(0);

  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);

  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(targetHeight);
  font.setBold(true);
  p.setFont(font);
  p.setPen(Qt::white);

  p.drawText(QRect(0, 0, canvasW, canvasH), Qt::AlignCenter, character);
  p.end();

  int minX = canvasW, maxX = -1, minY = canvasH, maxY = -1;
  for (int y = 0; y < canvasH; ++y) {
    const uint8_t *line = img.constScanLine(y);
    for (int x = 0; x < canvasW; ++x) {
      if (line[x] >= 100) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
      }
    }
  }

  if (maxX < minX || maxY < minY)
    return {};

  const int w = maxX - minX + 1;
  const int h = maxY - minY + 1;
  QImage cropped(w, h, QImage::Format_Grayscale8);
  for (int y = 0; y < h; ++y) {
    const uint8_t *src = img.constScanLine(minY + y) + minX;
    uint8_t *dst = cropped.scanLine(y);
    for (int x = 0; x < w; ++x) {
      dst[x] = (src[x] >= 100) ? 255 : 0;
    }
  }
  return cropped;
}

double matchCharTemplate(const QImage &charCrop, const QImage &tmpl) {
  if (charCrop.isNull() || tmpl.isNull())
    return 0.0;

  const int th = tmpl.height();
  const int tw = tmpl.width();
  const int ch = charCrop.height();
  const int cw = charCrop.width();

  if (ch < th || cw < tw)
    return 0.0;

  QImage cropGray = charCrop.convertToFormat(QImage::Format_Grayscale8);
  std::vector<uint8_t> bin(size_t(cw * ch), 0);
  int cropWhiteCount = 0;
  for (int y = 0; y < ch; ++y) {
    const uint8_t *line = cropGray.constScanLine(y);
    for (int x = 0; x < cw; ++x) {
      if (line[x] >= 140) {
        bin[size_t(y * cw + x)] = 1;
        cropWhiteCount++;
      }
    }
  }

  int tmplWhiteCount = 0;
  std::vector<uint8_t> tmplBin(size_t(tw * th), 0);
  for (int y = 0; y < th; ++y) {
    const uint8_t *line = tmpl.constScanLine(y);
    for (int x = 0; x < tw; ++x) {
      if (line[x] > 0) {
        tmplBin[size_t(y * tw + x)] = 1;
        tmplWhiteCount++;
      }
    }
  }

  if (tmplWhiteCount == 0 || cropWhiteCount == 0)
    return 0.0;

  double bestScore = 0.0;
  const int maxY = ch - th;
  const int maxX = std::min(16, cw - tw);

  for (int dy = 0; dy <= maxY; ++dy) {
    for (int dx = 0; dx <= maxX; ++dx) {
      int intersection = 0;
      int roiWhite = 0;

      for (int y = 0; y < th; ++y) {
        const int cy = dy + y;
        for (int x = 0; x < tw; ++x) {
          const int cx = dx + x;
          const uint8_t t = tmplBin[size_t(y * tw + x)];
          const uint8_t c = bin[size_t(cy * cw + cx)];
          if (c)
            roiWhite++;
          if (t && c)
            intersection++;
        }
      }

      if (intersection > 0 && roiWhite > 0) {
        const double precision = double(intersection) / double(roiWhite);
        const double recall = double(intersection) / double(tmplWhiteCount);
        const double f1 = 2.0 * precision * recall / (precision + recall);
        bestScore = std::max(bestScore, f1);
      }
    }
  }
  return bestScore;
}

TagCandidate matchClosestTag(const std::string &ocrText, const QImage &rowCrop, double itfScore,
                             const std::vector<std::string> &tags,
                             const std::vector<TagSample> &samples) {
  if (tags.empty())
    return {};

  std::map<std::string, double> scores;
  std::map<std::string, QString> methods;
  for (const auto &t : tags) {
    scores[t] = 0.0;
    methods[t] = QStringLiteral("確率推定");
  }

  // 1. 候補タグの動的文字当てはめ照合（Synthetic Template Matching）
  if (rowCrop.width() >= 40 && rowCrop.height() >= 35) {
    const QImage charArea = rowCrop.copy(12, 6, std::min(45, rowCrop.width() - 12),
                                         std::min(40, rowCrop.height() - 6));
    for (const auto &tag : tags) {
      const QString tagId = initialIdentifier(QString::fromStdString(tag));
      if (!tagId.isEmpty()) {
        const QImage tmpl = renderCharTemplate(tagId, 24);
        const double synScore = matchCharTemplate(charArea, tmpl);
        const double relativeScore = std::min(0.68, synScore * 0.72);
        if (relativeScore > scores[tag]) {
          scores[tag] = relativeScore;
          methods[tag] = QStringLiteral("文字当てはめ照合");
        }
      }
    }
  }

  // 2. 登録済み画像照合スコア（先頭1文字パッチ照合）
  if (!samples.empty()) {
    const QImage gray = rowCrop.convertToFormat(QImage::Format_Grayscale8);
    for (const auto &sample : samples) {
      std::string matchedTeam;
      for (const auto &tag : tags) {
        if (sample.tag == tag ||
            (!sample.tag.empty() && initialIdentifier(QString::fromStdString(sample.tag)) ==
                                        initialIdentifier(QString::fromStdString(tag)))) {
          matchedTeam = tag;
          break;
        }
      }
      if (matchedTeam.empty())
        continue;
      const QImage p = sample.patch.convertToFormat(QImage::Format_Grayscale8);
      if (p.width() < 8 || p.height() < 12 || p.width() > 240 || p.height() > 48)
        continue;
      double best = 0;
      for (int y = 0; y + p.height() <= gray.height(); ++y)
        for (int x = 0; x <= 50 && x + p.width() <= gray.width(); ++x)
          best = std::max(best, similarity(gray, p, x, y));
      if (best > scores[matchedTeam]) {
        scores[matchedTeam] = best;
        methods[matchedTeam] = QStringLiteral("画像照合");
      }
    }
  }

  // 2. 同梱ITFテンプレートスコア（先頭1文字）
  if (itfScore > 0.0) {
    for (const auto &tag : tags) {
      if (tag == "ITF." || tag == "ITF" ||
          initialIdentifier(QString::fromStdString(tag)) ==
              initialIdentifier(QStringLiteral("ITF."))) {
        if (itfScore > scores[tag]) {
          scores[tag] = itfScore;
          methods[tag] = QStringLiteral("同梱ITF画像");
        }
      }
    }
  }

  // 3. OCR比較（完全・部分一致、最初1文字目、類似文字、単語頭文字）
  if (!ocrText.empty()) {
    for (const auto &part : QString::fromStdString(ocrText).split('|')) {
      const QString candidate = part.trimmed();
      if (candidate.isEmpty())
        continue;
      const QString candidateNorm = normalized(candidate);
      const QString candidateId = initialIdentifier(candidate);

      for (const auto &tag : tags) {
        const QString tagStr = QString::fromStdString(tag).trimmed();
        if (tagStr.isEmpty())
          continue;
        const QString tagNorm = normalized(tagStr);
        const QString tagId = initialIdentifier(tagStr);

        double ocrScore = 0.0;
        QString ocrMethod = QStringLiteral("OCR推定");

        // A. タグ文字列そのものが含まれる場合（最高確信度）
        if (!tagNorm.isEmpty() && candidateNorm.contains(tagNorm)) {
          ocrScore = 0.95;
          ocrMethod = QStringLiteral("OCRタグ一致");
        } else if (!candidateId.isEmpty() && candidateId == tagId) {
          // B. 最初1文字目が一致
          ocrScore = 0.85;
          ocrMethod = QStringLiteral("OCR先頭1文字一致");
        } else if (!candidateId.isEmpty() && charSimilarity(candidateId, tagId) >= 0.70) {
          // C. 類似文字
          ocrScore = 0.68;
          ocrMethod = QStringLiteral("OCR類似文字");
        } else {
          // D. スペースや記号で区切られた単語の先頭文字をチェック
          for (const auto &token : candidate.split(QRegularExpression(QStringLiteral("[\\s_\\[\\]\\(\\)\\-]+")), Qt::SkipEmptyParts)) {
            const QString tokenId = initialIdentifier(token);
            if (!tokenId.isEmpty() && tokenId == tagId) {
              ocrScore = std::max(ocrScore, 0.65);
              ocrMethod = QStringLiteral("OCR単語頭文字一致");
            }
          }
        }

        if (ocrScore > scores[tag]) {
          scores[tag] = ocrScore;
          methods[tag] = ocrMethod;
        }
      }
    }
  }

  std::string bestTag;
  double bestScore = -1.0;
  for (const auto &entry : scores) {
    if (entry.second > bestScore) {
      bestScore = entry.second;
      bestTag = entry.first;
    }
  }

  return {bestTag, std::max(0.0, bestScore), methods[bestTag]};
}
std::vector<std::string> resolveTeamsWithCapacity(
    const std::vector<std::vector<std::pair<std::string, double>>> &rowScores,
    int playersPerTeam) {
  const size_t nRows = rowScores.size();
  if (nRows == 0 || playersPerTeam <= 0)
    return {};

  std::vector<std::vector<std::pair<std::string, double>>> prefs = rowScores;
  for (auto &row : prefs) {
    std::stable_sort(row.begin(), row.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
  }

  std::vector<size_t> currentChoice(nRows, 0);

  // 定員超過時に一番確率が低いプレイヤーを外し、次の類似度チームに割り振るループ
  for (int iter = 0; iter < 100; ++iter) {
    std::map<std::string, std::vector<size_t>> teamAssignments;
    for (size_t r = 0; r < nRows; ++r) {
      if (currentChoice[r] < prefs[r].size()) {
        teamAssignments[prefs[r][currentChoice[r]].first].push_back(r);
      }
    }

    bool anyOverflow = false;
    for (auto &entry : teamAssignments) {
      auto &assigned = entry.second;
      if (static_cast<int>(assigned.size()) > playersPerTeam) {
        anyOverflow = true;
        // 定員を超えたチームの中で、一番確率（スコア）が低いプレイヤーを特定
        size_t lowestRow = assigned[0];
        double minScore = prefs[lowestRow][currentChoice[lowestRow]].second;
        for (size_t r : assigned) {
          double s = prefs[r][currentChoice[r]].second;
          if (s < minScore) {
            minScore = s;
            lowestRow = r;
          }
        }
        // そのプレイヤーを外して、次の類似度のチームに再配属
        if (currentChoice[lowestRow] + 1 < prefs[lowestRow].size()) {
          currentChoice[lowestRow]++;
        }
      }
    }

    if (!anyOverflow)
      break;
  }

  std::vector<std::string> result(nRows);
  for (size_t r = 0; r < nRows; ++r) {
    if (currentChoice[r] < prefs[r].size()) {
      result[r] = prefs[r][currentChoice[r]].first;
    }
  }
  return result;
}
Analysis Analyzer::read(const QImage &source, const std::vector<std::string> &tags,
                        const std::vector<TagSample> &samples, bool ocr, bool layoutOnly) const {
  Analysis out;
  if (source.isNull()) {
    out.error = QStringLiteral("映像が空です");
    return out;
  }
  if (std::abs(double(source.width()) / source.height() - 16.0 / 9.0) > .03) {
    out.error = QStringLiteral(
        "元ゲーム映像が16:9ではありません。クロップ前の映像ソースを選択してください。");
    return out;
  }
  out.image = source.scaled(1920, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_ARGB32);
  mk::Image image(1920, 1080);
  std::copy(out.image.constBits(), out.image.constBits() + image.pixels.size(),
            image.pixels.begin());
  out.layout = reference_.read(image);
  if (layoutOnly) {
    // A finish banner can dim the bottom rank without hiding its digits. Normalize
    // contrast per scanline, and require the visible 12 glyph to beat every other
    // rank template. This detects a real glyph; it never assigns a missing name.
    if (out.layout.anchors == 11 && out.layout.pluses >= 10 && out.layout.anchor[11] < .65 &&
        std::all_of(out.layout.anchor.begin(), out.layout.anchor.begin() + 11,
                    [](double score) { return score >= .65; })) {
      double score = 0;
      if (reference_.visibleLastRank(image, &score)) {
        out.layout.anchor[11] = score;
        ++out.layout.anchors;
      }
    }
    // Layout-only recognition never invokes OCR or fills rows from team capacity.
    for (int row = 0; row < 12; ++row) {
      out.standings.push_back({row + 1, {}, 0});
      out.evidence.push_back({});
    }
    return out;
  }
  out.finished_in_twelfth = hasTwelfthFinishNotice(out.image);
  if (!out.complete())
    return out;

  for (int row = 0; row < 12; ++row) {
    RowEvidence evidence;

    if (out.finished_in_twelfth && row == 11 && !tags.empty()) {
      const std::string myTag = tags[0];
      evidence.closestTag = myTag;
      evidence.closestScore = 1.0;
      evidence.similarity = 1.0;
      evidence.method = QStringLiteral("自チーム12位確定通知");
      out.standings.push_back({12, myTag, 1.0});
      out.evidence.push_back(evidence);
      continue;
    }

    const auto crop = rowImage(out.image, row);
    std::string ocrText;
    if (ocr) {
      const auto gray = crop.copy(13, 2, 400, 50).convertToFormat(QImage::Format_Grayscale8);
      std::vector<uint8_t> pixels(size_t(gray.width() * gray.height()));
      for (int y = 0; y < gray.height(); ++y)
        std::copy_n(gray.constScanLine(y), gray.width(), pixels.data() + size_t(y * gray.width()));
      ocrText = recognizeNativeText(pixels, gray.width(), gray.height());
      evidence.text = QString::fromStdString(ocrText);
    }

    const double itfScore =
        (out.layout.tag[size_t(row)] >= .84) ? out.layout.tag[size_t(row)] : 0.0;


    const auto candidate = matchClosestTag(ocrText, crop, itfScore, tags, samples);
    evidence.closestTag = candidate.tag;
    evidence.closestScore = candidate.score;
    evidence.similarity = candidate.score;
    evidence.method = candidate.method;

    out.standings.push_back({row + 1, candidate.tag, candidate.score});
    out.evidence.push_back(evidence);
  }

  for (size_t r = 0; r < 12; ++r) {
    if (out.standings[r].confidence < 0.70) {
      out.standings[r].tag.clear();
      out.evidence[r].method = QStringLiteral("要確認");
    }
  }

  if (out.finished_in_twelfth && !tags.empty()) {
    const std::string myTag = tags[0];
    out.standings[11].tag = myTag;
    out.standings[11].confidence = 1.0;
    out.evidence[11].closestTag = myTag;
    out.evidence[11].closestScore = 1.0;
    out.evidence[11].similarity = 1.0;
    out.evidence[11].method = QStringLiteral("自チーム12位確定通知");
  }

  return out;
}
bool saveSamples(const QString &path, const std::vector<TagSample> &samples, QString &error) {
  QJsonArray entries;
  for (const auto &s : samples) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (!s.patch.save(&buffer, "PNG")) {
      error = QStringLiteral("画像の保存に失敗しました");
      return false;
    }
    entries.append(QJsonObject{{"tag", QString::fromStdString(s.tag)},
                               {"png", QString::fromLatin1(data.toBase64())}});
  }
  QSaveFile file(path);
  const auto data = QJsonDocument(QJsonObject{{"schema", 1}, {"samples", entries}}).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
    error = file.errorString();
    return false;
  }
  return true;
}
bool loadSamples(const QString &path, std::vector<TagSample> &samples, QString &error) {
  QFile file(path);
  if (!file.exists())
    return true;
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
    error = QStringLiteral("タグ画像を読み込めません");
    return false;
  }
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || doc.object().value("schema").toInt() != 1 ||
      !doc.object().value("samples").isArray()) {
    error = QStringLiteral("タグ画像ファイルが破損しています");
    return false;
  }
  std::vector<TagSample> loaded;
  for (const auto &v : doc.object().value("samples").toArray()) {
    const auto obj = v.toObject();
    const auto tag = obj.value("tag").toString();
    const auto data = QByteArray::fromBase64(obj.value("png").toString().toLatin1());
    if (data.size() > 100000) {
      error = QStringLiteral("タグ画像が大きすぎます");
      return false;
    }
    const auto patch = QImage::fromData(data, "PNG");
    if (tag.isEmpty() || tag.size() > 64 || patch.width() < 8 || patch.width() > 240 ||
        patch.height() < 12 || patch.height() > 48 || loaded.size() >= 144) {
      error = QStringLiteral("タグ画像の形式が不正です");
      return false;
    }
    loaded.push_back({tag.toStdString(), patch});
  }
  samples = std::move(loaded);
  return true;
}

QString formatScoreLine(const std::vector<std::string> &tags,
                        const std::map<std::string, int> &totals,
                        size_t raceCount,
                        int totalRaces) {
  if (tags.empty())
    return QString();

  const std::string myTag = tags[0];
  const int myScore = totals.count(myTag) ? totals.at(myTag) : 0;

  struct TeamEntry {
    std::string tag;
    int score;
    size_t origIndex;
  };

  std::vector<TeamEntry> entries;
  entries.reserve(tags.size());
  for (size_t i = 0; i < tags.size(); ++i) {
    const auto &t = tags[i];
    int sc = totals.count(t) ? totals.at(t) : 0;
    entries.push_back({t, sc, i});
  }

  // 合計点が高い順にソート。同点の場合は元のタグ順（自チーム優先）を維持
  std::stable_sort(entries.begin(), entries.end(), [](const TeamEntry &a, const TeamEntry &b) {
    return a.score > b.score;
  });

  // 自チームの現在順位（自チームより点数が高いチーム数 + 1）
  int rank = 1;
  for (const auto &e : entries) {
    if (e.score > myScore) {
      rank++;
    }
  }

  QStringList parts;
  for (const auto &e : entries) {
    if (e.tag == myTag) {
      parts << QString("【%1】%2").arg(QString::fromStdString(e.tag)).arg(e.score);
    } else {
      const int diff = myScore - e.score;
      QString diffStr;
      if (diff > 0) {
        diffStr = QString("(+%1)").arg(diff);
      } else if (diff < 0) {
        diffStr = QString("(%1)").arg(diff);
      } else {
        diffStr = QString("(±0)");
      }
      parts << QString("%1 %2 %3").arg(QString::fromStdString(e.tag)).arg(e.score).arg(diffStr);
    }
  }

  parts << QString("現在%1位").arg(rank);

  const int remaining = std::max(0, totalRaces - static_cast<int>(raceCount));
  parts << QString("＠%1").arg(remaining);

  return parts.join(QStringLiteral("／"));
}
} // namespace quickscore
