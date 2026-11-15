#include "analysis.hpp"
#include "finish-notice.hpp"
#include "result-gate.hpp"
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

  std::cout << "PASS gate, strict OCR matching, persistence, image shift/polarity, ambiguity, 1-char box, closest candidate, capacity reassignment, score line formatting, synthetic template matching, finish notice detection\n";
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
