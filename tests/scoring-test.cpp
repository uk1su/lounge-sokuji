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
  std::cout << "quickscore core tests passed\n";
}
