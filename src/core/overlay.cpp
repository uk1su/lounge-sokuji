#include "overlay.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <algorithm>

namespace quickscore {
namespace {

constexpr int kOverlayWidth = 840;
constexpr int kOverlayHeight = 160;
constexpr int kCornerRadius = 15;
constexpr int kMaxTeam2SingleRaceDiff = 40; // (15+12+10+9+8+7) - (1+2+3+4+5+6)

QString formatSignedDiff(int diff) {
  if (diff > 0)
    return QString("+%1").arg(diff);
  if (diff == 0)
    return QStringLiteral("±0");
  return QString::number(diff);
}

QFont makeOverlayFont(int pixelSize) {
  QFont font;
  font.setStyleHint(QFont::SansSerif);
  font.setPixelSize(pixelSize);
  font.setBold(true);
  return font;
}

void drawFittedCenteredText(QPainter &painter,
                            const QRect &rect,
                            int pixelSize,
                            const QString &text,
                            const QColor &color) {
  painter.save();
  painter.setFont(makeOverlayFont(pixelSize));
  painter.setPen(color);

  const QFontMetrics metrics(painter.font());
  const int textWidth = metrics.horizontalAdvance(text);
  const int maxWidth = std::max(1, rect.width() - 20);
  if (textWidth > maxWidth) {
    const double scaleX = static_cast<double>(maxWidth) / static_cast<double>(textWidth);
    painter.translate(rect.left() + 10.0, rect.top());
    painter.scale(scaleX, 1.0);
    painter.drawText(QRectF(0.0, 0.0, static_cast<double>(textWidth), rect.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text);
  } else {
    painter.drawText(rect, Qt::AlignCenter, text);
  }
  painter.restore();
}

void renderTwoTeamOverlay(QPainter &painter, const OverlayState &state) {
  constexpr int kTeamWidth = 150;
  constexpr int kScoreWidth = 130;
  constexpr int kSpaceWidth = 100;
  constexpr int kDifWidth = kTeamWidth + kScoreWidth;
  constexpr int kHeaderHeight = 50;
  constexpr int kBodyHeight = 80;
  constexpr int kInnerWidth = kSpaceWidth + (kTeamWidth * 2) + (kScoreWidth * 2); // 660
  constexpr int kInnerHeight = kHeaderHeight + kBodyHeight;                       // 130

  const QRect innerRect((kOverlayWidth - kInnerWidth) / 2, (kOverlayHeight - kInnerHeight) / 2,
                        kInnerWidth, kInnerHeight);

  QPainterPath clipPath;
  clipPath.addRoundedRect(QRectF(innerRect), kCornerRadius, kCornerRadius);
  painter.save();
  painter.setClipPath(clipPath);
  painter.fillRect(innerRect, QColor(0, 0, 0));

  if (state.winConfirmed) {
    const QRect winRect(innerRect.left(), innerRect.top(), kTeamWidth, kHeaderHeight);
    painter.fillRect(winRect, QColor(255, 255, 0));
    painter.setFont(makeOverlayFont(35));
    painter.setPen(QColor(255, 69, 0));
    painter.drawText(winRect, Qt::AlignCenter, QStringLiteral("win!"));
  }

  const auto &leftTeam = state.teamsOriginal[0];
  const auto &rightTeam = state.teamsOriginal[1];
  const int diff = leftTeam.score - rightTeam.score;
  const QColor diffColor = diff >= 0 ? QColor(255, 69, 0) : QColor(0, 191, 255);

  painter.setFont(makeOverlayFont(35));
  painter.setPen(diffColor);
  // CSS header p uses line-height: calc(10px + 50px) inside 50px box, shifting baseline slightly down.
  painter.drawText(QRect(innerRect.left(), innerRect.top() + 5, kDifWidth, kHeaderHeight),
                   Qt::AlignVCenter | Qt::AlignRight, formatSignedDiff(diff));

  painter.setPen(QColor(255, 215, 0));
  painter.drawText(
      QRect(innerRect.left() + kDifWidth + kSpaceWidth, innerRect.top() + 5, kDifWidth, kHeaderHeight),
      Qt::AlignVCenter | Qt::AlignLeft, QString("残レース:%1").arg(state.remainingRaces));

  const int bodyTop = innerRect.top() + kHeaderHeight;
  const QColor white(255, 255, 255);
  drawFittedCenteredText(painter, QRect(innerRect.left(), bodyTop, kTeamWidth, kBodyHeight), 50,
                         QString::fromStdString(leftTeam.tag), white);

  painter.setFont(makeOverlayFont(60));
  painter.setPen(white);
  painter.drawText(QRect(innerRect.left() + kTeamWidth, bodyTop, kScoreWidth, kBodyHeight),
                   Qt::AlignVCenter | Qt::AlignRight, QString::number(leftTeam.score));
  painter.drawText(QRect(innerRect.left() + kDifWidth, bodyTop, kSpaceWidth, kBodyHeight),
                   Qt::AlignCenter, QStringLiteral("-"));
  painter.drawText(
      QRect(innerRect.left() + kDifWidth + kSpaceWidth, bodyTop, kScoreWidth, kBodyHeight),
      Qt::AlignVCenter | Qt::AlignLeft, QString::number(rightTeam.score));

  drawFittedCenteredText(
      painter,
      QRect(innerRect.left() + kDifWidth + kSpaceWidth + kScoreWidth, bodyTop, kTeamWidth, kBodyHeight),
      50, QString::fromStdString(rightTeam.tag), white);

  painter.restore();
}

void renderMultiTeamOverlay(QPainter &painter, const OverlayState &state) {
  constexpr int kTeamWidth = 780 / 6; // 130
  constexpr int kDifSpaceWidth = kTeamWidth / 2;
  constexpr int kRowHeight = 45;
  constexpr int kDifHeight = 35;
  constexpr int kInnerHeight = (kRowHeight * 2) + kDifHeight; // 125
  const int innerWidth = state.teamCount * kTeamWidth;

  const QRect innerRect((kOverlayWidth - innerWidth) / 2, (kOverlayHeight - kInnerHeight) / 2,
                        innerWidth, kInnerHeight);

  QPainterPath clipPath;
  clipPath.addRoundedRect(QRectF(innerRect), kCornerRadius, kCornerRadius);
  painter.save();
  painter.setClipPath(clipPath);
  painter.fillRect(innerRect, QColor(0, 0, 0));

  const QColor white(255, 255, 255);
  const QColor borderColor(0x66, 0x66, 0x66);
  painter.setPen(QPen(borderColor, 1));
  painter.drawLine(innerRect.left(), innerRect.top() + kRowHeight, innerRect.right(),
                   innerRect.top() + kRowHeight);
  for (int i = 1; i < state.teamCount; ++i) {
    const int x = innerRect.left() + (i * kTeamWidth);
    painter.drawLine(x, innerRect.top(), x, innerRect.top() + (kRowHeight * 2) - 1);
  }

  for (int i = 0; i < state.teamCount; ++i) {
    const auto &entry = state.teamsSorted[static_cast<size_t>(i)];
    const int x = innerRect.left() + (i * kTeamWidth);
    drawFittedCenteredText(painter, QRect(x, innerRect.top(), kTeamWidth, kRowHeight), 35,
                           QString::fromStdString(entry.tag), white);

    painter.setFont(makeOverlayFont(35));
    painter.setPen(white);
    painter.drawText(QRect(x, innerRect.top() + kRowHeight, kTeamWidth, kRowHeight), Qt::AlignCenter,
                     QString::number(entry.score));
  }

  painter.setFont(makeOverlayFont(25));
  painter.setPen(white);
  const int difTop = innerRect.top() + (kRowHeight * 2);
  for (int i = 0; i + 1 < state.teamCount; ++i) {
    const int diff =
        state.teamsSorted[static_cast<size_t>(i)].score - state.teamsSorted[static_cast<size_t>(i + 1)].score;
    const int x = innerRect.left() + kDifSpaceWidth + (i * kTeamWidth);
    painter.drawText(QRect(x, difTop, kTeamWidth, kDifHeight), Qt::AlignCenter,
                     formatSignedDiff(diff));
  }

  painter.drawText(QRect(innerRect.left(), difTop, innerWidth - 10, kDifHeight),
                   Qt::AlignVCenter | Qt::AlignRight,
                   QString("残%1").arg(state.remainingRaces));

  painter.restore();
}

} // namespace

OverlayState buildOverlayState(const std::vector<std::string> &tags,
                               const std::map<std::string, int> &totals,
                               size_t raceCount,
                               int totalRaces,
                               uint64_t revision) {
  OverlayState state;
  state.raceCount = static_cast<int>(raceCount);
  state.totalRaces = totalRaces;
  state.remainingRaces = std::max(0, totalRaces - static_cast<int>(raceCount));
  state.revision = revision;

  const size_t n = tags.size();
  if (n != 2 && n != 3 && n != 4 && n != 6)
    return state;

  state.teamCount = static_cast<int>(n);
  state.teamsOriginal.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const auto it = totals.find(tags[i]);
    const int sc = it == totals.end() ? 0 : it->second;
    state.teamsOriginal.push_back(OverlayTeamEntry{tags[i], sc, static_cast<int>(i)});
  }

  state.teamsSorted = state.teamsOriginal;
  std::stable_sort(state.teamsSorted.begin(), state.teamsSorted.end(),
                   [](const OverlayTeamEntry &a, const OverlayTeamEntry &b) {
                     if (a.score != b.score)
                       return a.score > b.score;
                     return a.originalIndex < b.originalIndex;
                   });

  if (n == 2 && !state.teamsSorted.empty() && state.teamsSorted[0].originalIndex == 0) {
    const int diff = state.teamsSorted[0].score - state.teamsSorted[1].score;
    if (diff > state.remainingRaces * kMaxTeam2SingleRaceDiff)
      state.winConfirmed = true;
  }

  return state;
}

QJsonObject overlayStateToJson(const OverlayState &state) {
  auto encodeTeams = [](const std::vector<OverlayTeamEntry> &list) {
    QJsonArray arr;
    for (const auto &entry : list) {
      arr.append(QJsonObject{{"tag", QString::fromStdString(entry.tag)},
                             {"score", entry.score},
                             {"original_index", entry.originalIndex}});
    }
    return arr;
  };

  return QJsonObject{{"revision", static_cast<qint64>(state.revision)},
                     {"team_count", state.teamCount},
                     {"race_count", state.raceCount},
                     {"total_races", state.totalRaces},
                     {"remaining_races", state.remainingRaces},
                     {"win_confirmed", state.winConfirmed},
                     {"teams_original", encodeTeams(state.teamsOriginal)},
                     {"teams_sorted", encodeTeams(state.teamsSorted)}};
}

QByteArray overlayStateToJs(const OverlayState &state) {
  const QByteArray json = QJsonDocument(overlayStateToJson(state)).toJson(QJsonDocument::Compact);
  return QByteArray("window.__QUICKSCORE_OVERLAY__ = ") + json + QByteArray(";\n");
}

QImage renderOverlayImage(const OverlayState &state) {
  QImage image(kOverlayWidth, kOverlayHeight, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);

  if (state.teamCount != 2 && state.teamCount != 3 && state.teamCount != 4 && state.teamCount != 6)
    return image;
  if (static_cast<int>(state.teamsOriginal.size()) != state.teamCount ||
      static_cast<int>(state.teamsSorted.size()) != state.teamCount)
    return image;

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  if (state.teamCount == 2)
    renderTwoTeamOverlay(painter, state);
  else
    renderMultiTeamOverlay(painter, state);

  painter.end();
  return image;
}

} // namespace quickscore
