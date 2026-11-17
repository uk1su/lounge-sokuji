#include "analysis.hpp"
#include "course.hpp"
#include "finish-notice.hpp"
#include "overlay.hpp"
#include "result-gate.hpp"
#include "score-digits.hpp"
#include "session-store.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace quickscore;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void tests() {
  ResultGate g;
  require(!g.observe(true, true, "a", 100), "startup result must not commit");
  for (uint64_t t = 200; t <= 3400; t += 200)
    g.observe(false, false, "", t);
  require(g.armed(), "arm after continuously absent result");
  require(!g.observe(true, true, "a", 3600), "first frame");
  require(!g.observe(true, true, "a", 3900), "second frame");
  require(g.observe(true, true, "a", 4200), "third stable frame");
  g.lock();
  for (uint64_t t = 4500; t < 8000; t += 200)
    require(!g.observe(true, false, "", t), "OCR failure cannot unlock visible result");
  require(!g.observe(true, true, "b", 8200), "different totals cannot duplicate");
  g.pause();
  require(!g.observe(true, true, "a", 8500), "pause preserves duplicate lock");
  g.observe(false, false, "", 9000);
  g.observe(false, false, "", 15000);
  require(!g.armed(), "frame gap cannot establish absence");
  for (uint64_t t = 15200; t <= 18200; t += 200)
    g.observe(false, false, "", t);
  require(g.armed(), "next race armed");
  require(!g.observe(true, true, "a", 18400), "identical next race needs stability");
  g.observe(true, true, "a", 18700);
  require(g.observe(true, true, "a", 19000), "identical next race can commit");
  for (uint64_t t = 19200; t <= 22500; t += 200)
    g.observe(false, false, "", t);
  require(g.armed(), "re-armed for immediate finish test");
  require(g.observe(true, true, "decision", 22700, true),
          "twelfth-place immediate trigger commits on first frame");
  require(g.locked(), "immediate trigger locks gate");
  QString tagErr;
  require(!validateTeamTags({"A", "a"}, &tagErr), "rejection of A vs a");
  require(!validateTeamTags({"あ", "ア"}, &tagErr), "rejection of あ vs ア");
  require(validateTeamTags({"た", "ダ"}, &tagErr), "acceptance of た vs ダ");
  require(exactTag("AA player", {"A", "B"}) == "A", "AA... resolves to team A");
  require(exactTag("Ace player", {"A", "B"}) == "A", "Ace... resolves to team A");
  require(exactTag("A player", {"A", "B"}) == "A", "A... resolves to team A");
  require(exactTag("AA player", {"Ace", "Beta"}) == "Ace", "AA... resolves to team Ace");
  require(exactTag("Ace player", {"Ace", "Beta"}) == "Ace", "Ace... resolves to team Ace");
  require(exactTag("A player", {"Ace", "Beta"}) == "Ace", "A... resolves to team Ace");
  require(exactTag("player A", {"A", "B"}).empty(), "name suffix cannot match tag");
  require(exactTag("player Ace", {"Ace", "Beta"}).empty(), "substring inside name cannot match tag");
  require(exactTag("た player", {"た", "ダ"}) == "た", "た matches た");
  require(exactTag("ダ player", {"た", "ダ"}) == "ダ", "ダ matches ダ");
  require(exactTag("ABC name", {"ABC", "ABD"}) == "ABC", "exact prefix fallback");
  require(exactTag("ABE name", {"ABC", "ABD"}).empty(), "no fuzzy one-letter substitution");
  require(exactTag("A name|B name", {"A", "B"}).empty(),
          "conflicting OCR polarities require review");
  require(exactTag("Ａ name", {"A", "B"}) == "A", "NFKC fullwidth");
  require(exactTag("かない☆名前", {"かない", "じ"}) == "かない", "Japanese tag prefix");
  require(exactTag("A-B name", {"A-B", "AB"}) == "A-B", "punctuation preserved");
  QTemporaryDir dir;
  require(dir.isValid(), "temporary directory");
  Session session;
  session.configure({"A", "B"}, 6);
  std::vector<Standing> rows;
  for (int i = 0; i < 12; ++i)
    rows.push_back({i + 1, i < 6 ? "A" : "B", .9});
  require(session.commit(rows, false), "commit fixture");
  QString error;
  QJsonArray audit{QJsonObject{{"raw", "retained"}}};
  const auto path = dir.path() + "/session.json";
  require(storeSession(path, session, audit, error), "atomic save");
  Session restored;
  QJsonArray readAudit;
  require(loadSession(path, restored, readAudit, error), "restore");
  require(restored.totals() == session.totals() && readAudit == audit, "restored totals and audit");

  Session deferred;
  deferred.configure({"A", "B"}, 6);
  auto partial = rows;
  partial[0].tag.clear();
  partial[0].confidence = 0;
  deferred.deferFirstRace(partial);
  require(deferred.commit(rows, false), "commit race 2 while 1R pending");
  deferred.setTeamAdjustments({{"A", 2}, {"B", -2}});
  const auto deferredPath = dir.path() + "/session-deferred.json";
  require(storeSession(deferredPath, deferred, QJsonArray{}, error), "save deferred session");
  Session restoredDeferred;
  require(loadSession(deferredPath, restoredDeferred, readAudit, error), "load deferred session");
  require(restoredDeferred.hasPendingFirstRace() &&
              restoredDeferred.pendingFirstRaceStandings().size() == 12 &&
              restoredDeferred.races().size() == 1 &&
              restoredDeferred.totals() == deferred.totals(),
          "restored pendingFirstRace and teamAdjustments");
  QFile broken(path);
  require(broken.open(QIODevice::WriteOnly | QIODevice::Truncate), "open broken fixture");
  broken.write("{bad");
  broken.close();
  require(!loadSession(path, restored, readAudit, error), "corrupt file rejected");
  require(restored.races().size() == 1, "restore transactional");
  QImage pattern(24, 40, QImage::Format_Grayscale8);
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 24; ++x)
      pattern.scanLine(y)[x] = uint8_t((x * 43 + y * 17 + x * y * 7) % 256);
  QImage row(425, 54, QImage::Format_Grayscale8);
  row.fill(90);
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 24; ++x)
      row.scanLine(y + 8)[x + 22] = 255 - pattern.constScanLine(y)[x];
  std::vector<TagSample> samples{{"A", pattern}};
  require(Analyzer::matchSamples(row, {"A", "B"}, samples).first == "A",
          "shift and polarity invariant template");
  samples.push_back({"B", pattern});
  require(Analyzer::matchSamples(row, {"A", "B"}, samples).first.empty(),
          "ambiguous templates rejected");
  const auto samplePath = dir.path() + "/samples.json";
  require(saveSamples(samplePath, samples, error), "sample save");
  std::vector<TagSample> loaded;
  require(loadSamples(samplePath, loaded, error) && loaded.size() == 2, "sample restore");

  // Test tight 1-character bounding box detection
  const auto blankBox = detectFirstCharBox(QImage());
  require(blankBox.width == 36 && blankBox.left == 15, "default char box");
  QImage testGlyph(425, 54, QImage::Format_Grayscale8);
  testGlyph.fill(50);
  for (int y = 8; y < 42; ++y)
    for (int x = 16; x < 44; ++x)
      testGlyph.scanLine(y)[x] = 240; // ~28px wide glyph
  const auto glyphBox = detectFirstCharBox(testGlyph);
  require(glyphBox.left >= 14 && glyphBox.left <= 16, "char box left");
  require(glyphBox.width >= 26 && glyphBox.width <= 36, "tight 1-character width (<= 36px)");

  // Test character similarity (fuzzy closest matching)
  require(charSimilarity("A", "a") == 1.0, "identical char similarity");
  require(charSimilarity("0", "O") >= 0.70, "0 vs O visual similarity");
  require(charSimilarity("1", "l") >= 0.70, "1 vs l visual similarity");
  require(charSimilarity("A", "Z") <= 0.20, "distinct char low similarity");

  // Test probabilistic closest tag matching from user candidates
  const auto closest1 = matchClosestTag("0mega player", testGlyph, 0.0, {"Omega", "Beta"}, {});
  require(closest1.tag == "Omega" && closest1.score >= 0.60, "0mega matches Omega via 0/O similarity");
  const auto closest2 = matchClosestTag("Alpha player", testGlyph, 0.0, {"Alpha", "Beta"}, {});
  require(closest2.tag == "Alpha" && closest2.score >= 0.80, "Alpha matches Alpha via initial char");

  // Test capacity overflow reassignment (7 players want Team A, 5 want Team B -> lowest A moves to B)
  std::vector<std::vector<std::pair<std::string, double>>> overflowScores(12);
  // Rows 0..6 want A with varying scores; Row 6 has lowest score for A
  for (int r = 0; r < 6; ++r)
    overflowScores[r] = {{"A", 0.90 - r * 0.02}, {"B", 0.30}};
  overflowScores[6] = {{"A", 0.65}, {"B", 0.60}}; // Row 6 is weakest A, but decent B
  // Rows 7..11 want B
  for (int r = 7; r < 12; ++r)
    overflowScores[r] = {{"B", 0.85}, {"A", 0.20}};

  const auto assigned = resolveTeamsWithCapacity(overflowScores, 6);
  int countA = 0, countB = 0;
  for (const auto &team : assigned) {
    if (team == "A") ++countA;
    if (team == "B") ++countB;
  }
  require(countA == 6 && countB == 6, "capacity constraint exactly satisfied 6v6");
  require(assigned[6] == "B", "lowest probability player on overloaded team A reassigned to next best team B");

  // Test horizontal score format
  const std::vector<std::string> sixTags = {"A", "B", "C", "D", "E", "F"};
  const std::map<std::string, int> userExampleTotals = {
      {"A", 17}, {"B", 13}, {"C", 19}, {"D", 15}, {"E", 11}, {"F", 7}};
  const QString formatted = formatScoreLine(sixTags, userExampleTotals, 1, 12);
  require(formatted == QStringLiteral("C 19 (-2)／【A】17／D 15 (+2)／B 13 (+4)／E 11 (+6)／F 7 (+10)／現在2位／＠11"),
          "horizontal score line matches user format specification");

  // Test tie and 1st place
  const std::map<std::string, int> tieTotals = {
      {"A", 30}, {"B", 30}, {"C", 20}, {"D", 10}, {"E", 5}, {"F", 0}};
  const QString tieFormatted = formatScoreLine(sixTags, tieTotals, 2, 12);
  require(tieFormatted.contains(QStringLiteral("【A】30")), "my team has no diff in brackets");
  require(tieFormatted.contains(QStringLiteral("B 30 (±0)")), "tied team shows (±0)");
  require(tieFormatted.contains(QStringLiteral("現在1位")), "tied for 1st shows 1st");
  require(tieFormatted.contains(QStringLiteral("＠10")), "race 2 gives 10 remaining");

  // The first configured team is the user's team even when it is A in the
  // provided 2v2 result. Recognizer candidates and score sorting are separate.
  Session ownA;
  const std::vector<std::string> ownATags{"A", "M", "う", "L", "B", "R"};
  ownA.configure(ownATags, 2);
  const std::vector<std::string> actualTags{"M", "う", "A", "L", "B", "L",
                                           "R", "う", "A", "R", "M", "B"};
  std::vector<Standing> ownARows;
  for (int row = 0; row < 12; ++row)
    ownARows.push_back({row + 1, actualTags[size_t(row)], .99});
  require(ownA.commit(ownARows, false), "arbitrary own team A accepts real 2v2 standings");
  require(formatScoreLine(ownATags, ownA.totals(), 1, 12) ==
              QStringLiteral("M 17 (-3)／う 17 (-3)／L 16 (-2)／【A】14／B 9 (+5)／R 9 (+5)／現在4位／＠11"),
          "own team A score, differences, rank and original format");

  // Test dynamic synthetic template matching
  const auto tmplA = renderCharTemplate("A", 24);
  const auto tmplZ = renderCharTemplate("Z", 24);
  require(!tmplA.isNull() && !tmplZ.isNull(), "template rendering");
  const double selfA = matchCharTemplate(tmplA, tmplA);
  const double crossAZ = matchCharTemplate(tmplA, tmplZ);
  require(selfA >= 0.95, "identical char match >= 0.95");
  require(crossAZ < selfA - 0.25, "distinct char match discriminative");

  // Test finish notice text recognition
  require(isTwelfthFinishNoticeText("順位が確定したので レースを終了しました"), "exact finish notice");
  require(isTwelfthFinishNoticeText("順位が確定したので\nレースを終了しました"), "newline finish notice");
  require(isTwelfthFinishNoticeText("順位が確定したので　レースを終了しました"), "ideographic space finish notice");
  require(!isTwelfthFinishNoticeText("何か別のテキスト"), "non-matching notice text");

  // Test real screenshot if available
  const QString sampleScreen = qEnvironmentVariable("QUICKSCORE_FINISH_NOTICE_FIXTURE");
  if (!sampleScreen.isEmpty() && QFile::exists(sampleScreen)) {
    const QImage img(sampleScreen);
    require(!img.isNull(), "load 12th place sample screenshot");
    require(hasTwelfthFinishNotice(img), "detect 12th finish notice banner");

    Analyzer realAnalyzer("data/templates.mkr");
    const std::vector<std::string> testTags = {"B", "ITF", "NX", "TC", "G", "Z"};
    auto result = realAnalyzer.read(img, testTags, {}, false);
    require(result.finished_in_twelfth, "Analysis flags finished_in_twelfth");
    require(result.complete(), "Analysis is complete with 12th finish notice");
    require(result.standings.size() == 12, "12 standings present");
    require(result.standings[11].tag == "B", "12th place row is assigned to my team B");
    require(result.standings[11].confidence == 1.0, "12th place row confidence is 1.0");
    require(result.evidence[11].method == QStringLiteral("自チーム12位確定通知"), "evidence method indicates finish notice");
  }

  const QString screen2Sample =
      QStringLiteral("C:/Users/koto/AppData/Roaming/obs-studio/plugin_config/mk-rank-sender/captures/20261108-185649-355-held.png");
  if (QFile::exists(screen2Sample)) {
    const QImage s2(screen2Sample);
    require(!s2.isNull(), "load Screen 2 cumulative sample");
    const auto readS2 = readCumulativeScores(s2);
    const std::vector<int> expectedTotals{67, 63, 62, 55, 51, 51, 42, 39, 39, 38, 34, 33};
    require(readS2.valid() && readS2.totalPoints == 574 && readS2.impliedRaceCount() == 7,
            "Screen 2 cumulative totals valid and sum to 574 (7 races)");
    for (int i = 0; i < 12; ++i)
      require(readS2.rows[size_t(i)].points == expectedTotals[size_t(i)],
              "Screen 2 row cumulative points match");
    if (QFile::exists("data/templates.mkr")) {
      Analyzer cumAnalyzer("data/templates.mkr");
      const Analysis cumRes = cumAnalyzer.read(s2, {"A", "B"}, {}, false, true);
      require(cumRes.isCumulativeStandings() && cumRes.layout.anchors == 12,
              "Screen 2 tied sorted cumulative standings detected with all 12 anchors");
    }
  }

  {
    mk::Image synth(1920, 1080);
    std::fill(synth.pixels.begin(), synth.pixels.end(), uint8_t(30));
    auto drawBadge = [&](mk::Image &target, int row, int code) {
      const int y0 = int(std::lround(112.0 + 78.0 * row)) - 30;
      const int bx = 4 + (code % 4) * 15;
      const int by = 4 + (code / 4) * 19;
      for (int j = 0; j < 64; ++j) {
        for (int i = 0; i < 64; ++i) {
          const bool on = (i >= bx && i < bx + 10 && j >= by && j < by + 10);
          const uint8_t v = on ? 235 : 30;
          uint8_t *px = &target.pixels[(size_t(y0 + j) * 1920 + 837 + i) * 4];
          px[0] = px[1] = px[2] = v;
          px[3] = 255;
        }
      }
    };
    for (int r = 0; r < 12; ++r)
      drawBadge(synth, r, r);
    const auto rec = mk::Recognizer::calibrate(synth);

    mk::Image tied = synth;
    drawBadge(tied, 1, 0);
    drawBadge(tied, 9, 8);
    const auto layoutBefore = rec.readLayoutOnly(tied);
    require(layoutBefore.anchor[1] < 0.65 && layoutBefore.anchor[9] < 0.65,
            "tied rows fail exact row rank template match");
    double tiedScore1 = 0.0, tiedScore9 = 0.0;
    require(rec.visibleTiedRank(tied, 1, &tiedScore1) && tiedScore1 >= 0.72,
            "visibleTiedRank recognizes 1,1 tied badge on row 1");
    require(rec.visibleTiedRank(tied, 9, &tiedScore9) && tiedScore9 >= 0.72,
            "visibleTiedRank recognizes 9,9 tied badge on row 9");
    drawBadge(tied, 3, 10);
    require(!rec.visibleTiedRank(tied, 3),
            "visibleTiedRank rejects downstream rank template on earlier row");
  }

  {
    QTemporaryDir temp;
    require(temp.isValid(), "create temp dir for player override test");

    auto makeRowFrame = [](int row, const QColor &bg, const QColor &fg, const QString &text,
                           int dx = 0) {
      QImage img(1920, 1080, QImage::Format_ARGB32);
      img.fill(QColor(18, 22, 28));
      QPainter p(&img);
      p.fillRect(1000, 112 + 78 * row - 28, 435, 56, bg);
      QFont font("Sans", 22, QFont::Bold);
      p.setFont(font);
      p.setPen(fg);
      p.drawText(QRect(1005 + 16 + dx, 112 + 78 * row - 27 + 6, 300, 42),
                 Qt::AlignLeft | Qt::AlignVCenter, text);
      return img;
    };

    const QImage frameDark = makeRowFrame(0, QColor(30, 34, 42), Qt::white, QStringLiteral("Mimosa_99"));
    const QImage frameYellow =
        makeRowFrame(3, QColor(246, 226, 72), Qt::black, QStringLiteral("Mimosa_99"), 2);
    const QImage frameOther = makeRowFrame(5, QColor(30, 34, 42), Qt::white, QStringLiteral("KuroNeko"));

    const QImage patch = extractPlayerNamePatch(frameDark, 0);
    require(!patch.isNull() && patch.size() == QSize(320, 42), "player name patch size 320x42");

    std::vector<TagSample> overrides{TagSample{"M", patch}};
    QString err;
    const QString ovPath = temp.filePath("player-overrides.json");
    require(saveSamples(ovPath, overrides, err), "save 320x42 override patch");
    std::vector<TagSample> reloaded;
    require(loadSamples(ovPath, reloaded, err) && reloaded.size() == 1 &&
                reloaded[0].patch.size() == QSize(320, 42),
            "load 320x42 override patch");

    const double yellowScore = matchPlayerNameEdge(frameYellow, 3, reloaded[0].patch);
    const double otherScore = matchPlayerNameEdge(frameOther, 5, reloaded[0].patch);
    require(yellowScore >= 0.80, "polarity-inverted yellow row matches learned player patch");
    require(yellowScore - otherScore >= 0.15, "different player name scores lower than learned patch");

    Analysis a;
    a.image = frameYellow;
    a.standings.resize(12);
    a.evidence.resize(12);
    for (int i = 0; i < 12; ++i)
      a.standings[size_t(i)] = {i + 1, (i == 3) ? std::string{} : std::string("A"), 1.0};
    a.standings[3].confidence = 0.0;
    a.error = QStringLiteral("1人以上チーム不明");

    applyPlayerOverrides(a, reloaded, {"M", "う", "A", "L", "B", "R"});
    require(a.standings[3].tag == "M", "applyPlayerOverrides assigns learned tag on matching row");
    require(a.error.isEmpty(), "applyPlayerOverrides clears unresolved error once all rows tagged");
    require(a.evidence[3].method.contains("手動学習補完"), "evidence records learned override");
  }

  {
    require(quickscore::CourseRecognizer::matchCourseName("Wii レインボーロード") ==
                std::optional<std::string>("Wiiレインボーロード"),
            "course fuzzy match preserves Wii Rainbow Road");
    require(quickscore::CourseRecognizer::matchCourseName("Tour 東京スクランブル") ==
                std::optional<std::string>("トーキョースクランブル") ||
                quickscore::CourseRecognizer::matchCourseName("トーキョースクランブル") ==
                    std::optional<std::string>("トーキョースクランブル"),
            "course match matches canonical Tokyo Scramble");
    require(quickscore::CourseRecognizer::matchCourseName("ウォーター パーク") ==
                std::optional<std::string>("ウォーターパーク"),
            "course match strips whitespace");
    require(quickscore::CourseRecognizer::matchCourseName("スカイカーテン") ==
                std::optional<std::string>("スカイガーデン"),
            "course match tolerates voiced/unvoiced kana errors for Sky Garden");
    require(quickscore::CourseRecognizer::matchCourseName("スカイガーデン") ==
                std::optional<std::string>("スカイガーデン"),
            "normal Sky Garden must not become GBA");
    require(quickscore::CourseRecognizer::matchCourseName("マリオサーキット3") ==
                std::optional<std::string>("SFCマリオサーキット3"),
            "SFC Mario Circuit 3 matches without prefix");
    require(quickscore::CourseRecognizer::matchCourseName("5FCマリオサーキット3") ==
                std::optional<std::string>("SFCマリオサーキット3"),
            "SFC Mario Circuit 3 normalizes 5FC prefix");
    require(quickscore::CourseRecognizer::matchCourseName("SEC マリオサーキット3") ==
                std::optional<std::string>("SFCマリオサーキット3"),
            "SFC Mario Circuit 3 normalizes SEC prefix");
    require(quickscore::CourseRecognizer::matchCourseName("マリオサーキット") ==
                std::optional<std::string>("マリオサーキット"),
            "normal Mario Circuit stays distinct from SFC3");
    require(quickscore::CourseRecognizer::matchCourseName("クラクラかさん") ==
                std::optional<std::string>("グラグラかざん"),
            "course match tolerates voiced hiragana/katakana mix");
    require(quickscore::CourseRecognizer::matchCourseName("3DS レインボーロード") ==
                std::optional<std::string>("3DSレインボーロード"),
            "course match keeps 3DS prefix distinct from Wii");
    require(quickscore::CourseRecognizer::matchCourseName("ＧＢＡ スカイガーデン") ==
                std::optional<std::string>("GBAスカイガーデン"),
            "course match normalizes fullwidth ASCII prefix");
    require(quickscore::CourseRecognizer::matchCourseName("Tour ベルリンシュトラーセ") ==
                std::optional<std::string>("ベルリンシュトラーセ"),
            "course match handles Tour city prefix with normalized codepoints");

    QImage courseImg(1920, 1080, QImage::Format_ARGB32);
    courseImg.fill(Qt::black);
    {
      QPainter cp(&courseImg);
      cp.fillRect(450, 945, 1000, 90, QColor(175, 175, 175));
    }
    require(quickscore::CourseRecognizer::isCourseScreen(courseImg),
            "course strip banner detected on dark frame with gray strip");
    QImage darkStripImg(1920, 1080, QImage::Format_ARGB32);
    darkStripImg.fill(Qt::black);
    {
      QPainter cp(&darkStripImg);
      cp.fillRect(450, 945, 1000, 90, QColor(142, 145, 140));
    }
    require(quickscore::CourseRecognizer::isCourseScreen(darkStripImg),
            "course strip banner tolerates darker gray and slight chroma shift");
    require(!quickscore::CourseRecognizer::isCourseScreen(QImage(1920, 1080, QImage::Format_ARGB32)),
            "uninitialized black/white image handled cleanly");

    quickscore::CourseGate cg;
    require(!cg.observe(std::string("ウォーターパーク"), 100, false).has_value() &&
                !cg.observe(std::string("ウォーターパーク"), 300, false).has_value(),
            "CourseGate does not lock while allow=false");
    require(cg.observe(std::string("ウォーターパーク"), 500, true) ==
                std::optional<std::string>("ウォーターパーク"),
            "CourseGate locks once allow becomes true on subsequent frame");
    require(!cg.observe(std::string("ウォーターパーク"), 700, true).has_value(),
            "CourseGate does not re-emit identical locked course");
    require(!cg.observe(std::string("ビッグブルー"), 900, true).has_value() &&
                cg.observe(std::string("ビッグブルー"), 1100, true) ==
                    std::optional<std::string>("ビッグブルー"),
            "CourseGate re-emits when a different course stabilizes for 2 frames");
    cg.reset();
    require(cg.observe(std::string("ビッグブルー"), 1300) == std::nullopt &&
                cg.observe(std::string("ビッグブルー"), 1500) ==
                    std::optional<std::string>("ビッグブルー"),
            "CourseGate resets between races");
    cg.reset();
    require(cg.observe(std::string("サンセットこうや"), 1700) == std::nullopt &&
                cg.observe(std::nullopt, 1850) == std::nullopt &&
                cg.observe(std::string("サンセットこうや"), 2000) ==
                    std::optional<std::string>("サンセットこうや"),
            "CourseGate tolerates a single transient OCR miss");
    cg.reset();
    require(cg.observe(std::string("サンセットこうや"), 2200) == std::nullopt &&
                cg.observe(std::nullopt, 2350) == std::nullopt &&
                cg.observe(std::nullopt, 2500) == std::nullopt &&
                cg.observe(std::string("サンセットこうや"), 2650) == std::nullopt,
            "CourseGate resets after two consecutive OCR misses");

    quickscore::CourseRecognizer cr;
    const std::vector<std::pair<QString, std::string>> realCourseFixtures = {
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-09-05 22-36-24.png"),
         "ワリオスタジアム"},
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-09-05 22-38-27.png"),
         "ニンニンドージョー"},
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-09-05 22-38-49.png"),
         "カラカラさばく"},
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-09-06 15-05-28.png"),
         "シャーベットランド"},
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-09-06 15-05-39.png"),
         "ツルツルツイスター"},
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-09-06 15-05-49.png"),
         "ネオクッパシティ"},
        {QStringLiteral("C:/Users/koto/Documents/Codex/2026-09-05/cod/work/test_sfc_mc3_1450.png"),
         "SFCマリオサーキット3"},
        {QStringLiteral("C:/Users/koto/Downloads/Screenshot 2026-11-10 SFC-Mario-Circuit-3.png"),
         "SFCマリオサーキット3"},
    };
    for (const auto &[path, expected] : realCourseFixtures) {
      if (QFile::exists(path)) {
        const QImage img(path);
        require(!img.isNull(), "load real course screenshot fixture");
        require(quickscore::CourseRecognizer::isCourseScreen(img),
                "real course screenshot detected by isCourseScreen");
        const auto got = cr.recognize(img);
        require(got == std::optional<std::string>(expected),
                "real course screenshot matched canonical name");
      }
    }
  }

  {
    const auto st6 = quickscore::buildOverlayState(
        {"M", "う", "A", "L", "B", "R"},
        {{"M", 68}, {"う", 68}, {"A", 56}, {"L", 64}, {"B", 36}, {"R", 36}}, 4, 12, 7);
    require(st6.teamCount == 6 && st6.remainingRaces == 8 && !st6.winConfirmed,
            "overlay 6-team metadata");
    require(st6.teamsSorted.size() == 6 && st6.teamsSorted[0].tag == "M" &&
                st6.teamsSorted[1].tag == "う" && st6.teamsSorted[2].tag == "L" &&
                st6.teamsSorted[3].tag == "A" && st6.teamsSorted[4].tag == "B" &&
                st6.teamsSorted[5].tag == "R",
            "overlay 6-team sorts descending with original tag tie-break");
    const QImage img6 = quickscore::renderOverlayImage(st6);
    require(img6.size() == QSize(840, 160), "overlay image is 840x160");
    require(qAlpha(img6.pixel(0, 0)) == 0, "overlay outer corner is transparent");
    require(qAlpha(img6.pixel(420, 80)) == 255, "overlay inner card is opaque");

    const auto st2Win = quickscore::buildOverlayState(
        {"AAA", "BBB"}, {{"AAA", 510}, {"BBB", 460}}, 11, 12, 8);
    require(st2Win.teamCount == 2 && st2Win.remainingRaces == 1 && st2Win.winConfirmed,
            "overlay 2-team detects win confirmed when lead exceeds remaining*40");
    const QImage img2 = quickscore::renderOverlayImage(st2Win);
    require(img2.size() == QSize(840, 160), "2-team overlay image is 840x160");
    require(qRed(img2.pixel(115, 25)) == 255 && qGreen(img2.pixel(115, 25)) == 255 &&
                qBlue(img2.pixel(115, 25)) == 0,
            "2-team win badge renders yellow corner box");
    const auto st2Json = quickscore::overlayStateToJson(st2Win);
    require(st2Json.value("win_confirmed").toBool() && st2Json.value("remaining_races").toInt() == 1,
            "overlay JSON includes win_confirmed and remaining_races");
    require(quickscore::overlayStateToJs(st2Win).startsWith("window.__QUICKSCORE_OVERLAY__ = "),
            "overlay JS assigns global state");
  }

  {
    quickscore::Analysis cpuCase;
    cpuCase.error = QStringLiteral("12人中1人不明");
    cpuCase.standings = {
        {1, "A", 0.99}, {2, "B", 0.98},  {3, "C", 0.97}, {4, "D", 0.96},
        {5, "A", 0.99}, {6, "", 0.0},    {7, "C", 0.95}, {8, "D", 0.94},
        {9, "A", 0.98}, {10, "B", 0.97}, {11, "C", 0.96}, {12, "D", 0.95}};
    cpuCase.evidence.resize(12);
    cpuCase.evidence[5].text = QStringLiteral("マリオ");
    require(quickscore::resolveSingleDeficitRow(cpuCase, {"A", "B", "C", "D"}, 3),
            "resolveSingleDeficitRow assigns single unmatched CPU row to deficit team");
    require(cpuCase.standings[5].tag == "B", "CPU slot resolved to team B");
    require(cpuCase.evidence[5].method == QStringLiteral("欠員・CPU自動補完"),
            "CPU slot evidence marks CPU auto-fill");
    require(cpuCase.error.isEmpty(), "CPU resolution clears single-slot error");

    quickscore::Analysis twoMissing = cpuCase;
    twoMissing.standings[5].tag.clear();
    twoMissing.standings[5].confidence = 0.0;
    twoMissing.standings[1].tag.clear();
    twoMissing.standings[1].confidence = 0.0;
    require(!quickscore::resolveSingleDeficitRow(twoMissing, {"A", "B", "C", "D"}, 3),
            "resolveSingleDeficitRow rejects 2 missing rows");
  }

  {
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir for session store");
    quickscore::Session s;
    s.configure({"A", "B", "C", "D"}, 3);
    require(s.importPriorSummary(2, {{"A", 50}, {"B", 44}, {"C", 38}, {"D", 32}}),
            "importPriorSummary succeeds");
    s.setInGameScoreOffset(164);
    require(s.commit({{1, "A", 1}, {2, "B", 1}, {3, "C", 1}, {4, "D", 1},
                      {5, "A", 1}, {6, "B", 1}, {7, "C", 1}, {8, "D", 1},
                      {9, "A", 1}, {10, "B", 1}, {11, "C", 1}, {12, "D", 1}},
                     false, nullptr, "ウォーターパーク"),
            "commit race 3 after 2R summary");
    QString err;
    const QString path = dir.filePath("session.json");
    require(quickscore::storeSession(path, s, {}, err), "storeSession succeeds");
    quickscore::Session loaded;
    QJsonArray audit;
    require(quickscore::loadSession(path, loaded, audit, err), "loadSession succeeds");
    require(loaded.inGameScoreOffset() == 164, "inGameScoreOffset preserved");
    require(loaded.effectiveRaceCount() == 3, "effectiveRaceCount preserved across reload");
    require(loaded.races().size() == 2 && loaded.races()[0].raceSpan == 2 &&
                loaded.races()[0].label == "1〜2R合計" && loaded.races()[1].number == 3,
            "summary race metadata and race numbering preserved");
    require(loaded.totals() == s.totals(), "totals preserved across session store");
  }

  std::cout << "PASS gate, strict OCR matching, persistence, image shift/polarity, ambiguity, 1-char box, closest candidate, capacity reassignment, score line formatting, synthetic template matching, finish notice detection, player name edge override, course recognition, overlay state and image\n";
}
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--notice-gate") {
      Analyzer analyzer(args[2]);
      const QImage image(args[3]);
      require(!image.isNull(), "load user 2v2 notice fixture");
      const std::vector<std::string> tags{"M", "う", "A", "L", "B", "R"};
      const auto result = analyzer.read(image, tags, {}, false, true);
      require(result.complete() && result.layout.anchors == 12, "visible dimmed 12 glyph detected");
      for (const auto &standing : result.standings)
        require(standing.tag.empty(), "layout detector never fills a team");
      QImage hidden = image.copy();
      { QPainter painter(&hidden); painter.fillRect(QRect(827, 930, 90, 78), Qt::black); }
      require(!analyzer.read(hidden, tags, {}, false, true).complete(), "hidden rank cannot pass");
      QImage wrong = image.copy();
      { QPainter painter(&wrong); painter.drawImage(QPoint(837, 940), image.copy(837, 862, 64, 64)); }
      require(!analyzer.read(wrong, tags, {}, false, true).complete(), "11 glyph in final row rejected");
      std::cout << "PASS 2v2 finish banner gate, no team fill, hidden rank rejection, wrong rank rejection\n";
      return 0;
    }
    if (args.size() == 2 && args[1] == "--self-test") {
      tests();
      return 0;
    }
    if (args.size() == 4 && args[1] == "--render-overlay") {
      const auto st2 = buildOverlayState({"AAA", "BBB"}, {{"AAA", 510}, {"BBB", 460}}, 11, 12, 1);
      const auto st6 = buildOverlayState(
          {"M", "う", "A", "L", "B", "R"},
          {{"M", 68}, {"う", 68}, {"L", 64}, {"A", 56}, {"B", 36}, {"R", 36}}, 4, 12, 2);
      require(renderOverlayImage(st2).save(args[2]), "save 2-team overlay preview");
      require(renderOverlayImage(st6).save(args[3]), "save 6-team overlay preview");
      return 0;
    }
    if (args.size() == 8 && args[1] == "--train") {
      auto image =
          QImage(args[2]).scaled(1920, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      auto patch = Analyzer::rowImage(image, args[3].toInt() - 1)
                       .copy(args[5].toInt(), 6, args[6].toInt(), 40);
      QString error;
      require(saveSamples(args[7], {{args[4].toStdString(), patch}}, error), "save training crop");
      return 0;
    }
    if (args.size() < 4) {
      std::cerr << "analysis-test templates.mkr image.png tag1,tag2 [samples.json] [--no-ocr]\n";
      return 2;
    }
    Analyzer analyzer(args[1]);
    std::vector<std::string> tags;
    for (const auto &s : args[3].split(','))
      tags.push_back(s.toStdString());
    std::vector<TagSample> samples;
    QString error;
    if (args.size() > 4 && !args[4].startsWith("--"))
      require(loadSamples(args[4], samples, error), "load samples");
    QElapsedTimer timer;
    timer.start();
    auto a = analyzer.read(QImage(args[2]), tags, samples, !args.contains("--no-ocr"));
    QJsonArray rows;
    for (size_t i = 0; i < a.standings.size(); ++i)
      rows.append(QJsonObject{{"rank", a.standings[i].rank},
                              {"tag", QString::fromStdString(a.standings[i].tag)},
                              {"confidence", a.standings[i].confidence},
                              {"text", a.evidence[i].text},
                              {"method", a.evidence[i].method},
                              {"similarity", a.evidence[i].similarity}});
    const auto json = QJsonDocument(QJsonObject{{"anchors", a.layout.anchors},
                                                {"pluses", a.layout.pluses},
                                                {"visible", a.layout.visible},
                                                {"complete", a.complete()},
                                                {"ms", timer.elapsed()},
                                                {"error", a.error},
                                                {"rows", rows}})
                          .toJson();
    std::cout << json.constData();
    return a.error.isEmpty() ? 0 : 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
