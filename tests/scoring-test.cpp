#include "scoring.hpp"

#include <cassert>
#include <iostream>

using quickscore::Standing;

int main()
{
  quickscore::Session session;
  session.configure({"A", "B", "C", "D"}, 3);
  std::vector<Standing> valid{{1, "A", .99}, {2, "B", .98}, {3, "A", .97}, {4, "C", .99},
                              {5, "B", .96}, {6, "D", .99}, {7, "A", .95}, {8, "C", .99},
                              {9, "B", .94}, {10, "D", .99}, {11, "C", .93}, {12, "D", .92}};
  assert(session.validate(valid).ok);
  assert(session.commit(valid, false));
  const auto totals = session.totals();
  assert(totals.at("A") == 31);
  assert(totals.at("B") == 24);
  assert(totals.at("C") == 16);
  assert(totals.at("D") == 11);

  auto invalid = valid;
  invalid.back().tag = "A";
  assert(!session.validate(invalid).ok);
  assert(!session.commit(invalid, false));
  assert(session.races().size() == 1);
  assert(session.undo());
  assert(session.races().empty());
  assert(!session.undo());

  // 1) Defer Race 1 with an unknown player (rank 2 = +12 pts), commit Race 2,
  //    then recover Race 1 via Screen 2 cumulative totals (82 * 2 = 164).
  auto partialR1 = valid;
  partialR1[1].tag.clear();
  partialR1[1].confidence = 0.0;
  session.deferFirstRace(partialR1);
  assert(session.hasPendingFirstRace());
  assert(session.effectiveRaceCount() == 1);

  assert(session.commit(valid, false));
  assert(session.races().size() == 1);
  assert(session.races()[0].number == 2);
  assert(session.effectiveRaceCount() == 2);

  const std::map<std::string, int> screen2GameTotals{
      {"A", 62}, {"B", 48}, {"C", 32}, {"D", 22}};
  bool reconciledChanged = false;
  assert(session.reconcileWithCumulativeTotals(screen2GameTotals, 2, &reconciledChanged));
  assert(reconciledChanged);
  assert(!session.hasPendingFirstRace());
  assert(session.races().size() == 2);
  assert(session.races()[0].number == 1);
  assert(session.races()[1].number == 2);
  assert(session.races()[0].corrected);
  assert(session.races()[0].standings[1].tag == "B");
  assert(session.totals() == screen2GameTotals);

  // 2) Replace Race 1 manually (simulating correcting a wrong tag earlier) and
  //    then reconcile against cumulative Screen 2 totals.
  auto fixedR1 = valid;
  std::swap(fixedR1[0].tag, fixedR1[1].tag); // swaps A (+15) and B (+12) => A-3, B+3
  assert(session.replaceRace(0, fixedR1, true));
  assert(session.totals().at("A") == 59);
  assert(session.totals().at("B") == 51);
  // If Screen 2 shows game totals are still 62 / 48, reconciliation adjusts delta cleanly:
  reconciledChanged = false;
  assert(session.reconcileWithCumulativeTotals(screen2GameTotals, 2, &reconciledChanged));
  assert(reconciledChanged);
  assert(session.totals() == screen2GameTotals);
  assert(session.teamAdjustments().at("A") == 3);
  assert(session.teamAdjustments().at("B") == -3);

  // 3) Course preservation + 12-race match count
  quickscore::Session twelve;
  twelve.configure({"A", "B", "C", "D"}, 3);
  for (int i = 0; i < 12; ++i) {
    assert(twelve.commit(valid, false, nullptr, i == 0 ? "ウォーターパーク" : "ビッグブルー"));
  }
  assert(twelve.races().size() == 12);
  assert(twelve.races()[0].course == "ウォーターパーク");
  assert(twelve.toJson().find("\"course\": \"ウォーターパーク\"") != std::string::npos);

  // 4) Late start at 3R: import 1-2R summary (span=2, 164 pts), commit 3R..12R -> 12 effective races
  quickscore::Session late;
  late.configure({"A", "B", "C", "D"}, 3);
  const std::map<std::string, int> prior2R{{"A", 50}, {"B", 44}, {"C", 38}, {"D", 32}};
  assert(late.importPriorSummary(2, prior2R));
  assert(late.races().size() == 1);
  assert(late.races()[0].raceSpan == 2);
  assert(late.races()[0].number == 2);
  assert(late.races()[0].label == "1〜2R合計");
  assert(late.effectiveRaceCount() == 2);
  for (int r = 3; r <= 12; ++r)
    assert(late.commit(valid, false));
  assert(late.races().size() == 11);
  assert(late.races().back().number == 12);
  assert(late.effectiveRaceCount() == 12);
  assert(late.totals().at("A") == 50 + 31 * 10);

  // 5) Late start fallback reconciliation when 3R committed before summary
  quickscore::Session lateFallback;
  lateFallback.configure({"A", "B", "C", "D"}, 3);
  assert(lateFallback.commit(valid, false));
  assert(lateFallback.races().size() == 1);
  assert(lateFallback.races()[0].number == 1);
  const std::map<std::string, int> after3RGameTotals{
      {"A", 50 + 31}, {"B", 44 + 24}, {"C", 38 + 16}, {"D", 32 + 11}};
  bool fallbackChanged = false;
  assert(lateFallback.reconcileWithCumulativeTotals(after3RGameTotals, 3, &fallbackChanged));
  assert(fallbackChanged);
  assert(lateFallback.races().size() == 2);
  assert(lateFallback.races()[0].raceSpan == 2);
  assert(lateFallback.races()[0].label == "1〜2R合計");
  assert(lateFallback.races()[1].number == 3);
  assert(lateFallback.effectiveRaceCount() == 3);
  assert(lateFallback.totals() == after3RGameTotals);

  // 6) In-game 0-pt reset offset (DC / Room crash) preserves Sokuji totals and blocks bogus audit
  lateFallback.addInGameScoreOffset(246);
  assert(lateFallback.inGameScoreOffset() == 246);
  bool blockedAuditChanged = true;
  assert(!lateFallback.reconcileWithCumulativeTotals(after3RGameTotals, 3, &blockedAuditChanged));
  assert(!blockedAuditChanged);
  assert(lateFallback.totals() == after3RGameTotals);

  // 7) Auto-confirmed balanced race unlocks single-deficit elimination only after an uncorrected single race
  quickscore::Session gateSession;
  gateSession.configure({"A", "B", "C", "D"}, 3);
  assert(!gateSession.hasAutoConfirmedRace());
  assert(gateSession.commit(valid, true)); // manual correction should not unlock auto-elimination yet
  assert(!gateSession.hasAutoConfirmedRace());
  assert(gateSession.commit(valid, false)); // uncorrected single race unlocks it
  assert(gateSession.hasAutoConfirmedRace());
  gateSession.reset();
  assert(!gateSession.hasAutoConfirmedRace());

  std::cout << "quickscore core tests passed\n";
}
