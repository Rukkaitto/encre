// GROUPING, which is the screen's job and not the scan's -- the card holds raw runs
// because doing this per chapter would freeze decisions later chapters should be
// able to change: `Fran` is unambiguous until `Frank` appears, thirty chapters on.
//
// TWO OF THE PROBE'S ELEVEN RULES LIVE HERE. The other three of its grouping rules
// -- earliest-of-best-TIER, the lowercase-after-comma front-matter test, and the
// fuller-name tie-break -- do not come across at all, because every one of them
// exists to choose ONE introducing sentence per group and nothing chooses one any
// more: a name opens a list of its first eight sightings and the reader picks.
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/name_store.h"
#include "reader/names.h"

using reader::NameGroup;
using reader::NameIndexEntry;

namespace {

NameIndexEntry run(std::string text, int mid, int opening = 0, int total = -1) {
  NameIndexEntry e;
  e.text = std::move(text);
  e.midSentence = mid;
  e.chapterOpening = opening;
  e.total = total < 0 ? mid : total;
  return e;
}

const NameGroup* byDisplay(const std::vector<NameGroup>& g, const std::string& d) {
  for (const auto& x : g) {
    if (x.display == d) return &x;
  }
  return nullptr;
}

bool hasMember(const NameGroup& g, const std::string& m) {
  for (const auto& x : g.members) {
    if (x == m) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("containment joins a bare name to its fullest form, with no ratio guard") {
  // THE SINGLE LARGEST IMPROVEMENT IN THE PROBE. The guard existed so `Larry` would
  // not be LABELLED "Larry Underwood" -- the book calls him Larry -- but the label
  // is chosen separately, from the most-mentioned member, so the guard was only ever
  // costing a group its other members.
  const auto g = reader::groupNames({run("Larry", 400), run("Larry Underwood", 40),
                                     run("Underwood", 20)},
                                    5);
  REQUIRE(g.size() == 1);
  CHECK(g[0].display == "Larry");          // what is ON THE PAGE is findable
  CHECK(g[0].fullest == "Larry Underwood");  // ...and the full name is the answer
  CHECK(g[0].midSentence == 460);
  CHECK(hasMember(g[0], "Underwood"));

  SUBCASE("only the BEST run per token is joined, so a family is not one person") {
    // `Goldsmith` joins ONE Goldsmith. Linking every run containing the token would
    // collapse a family into a single entity.
    const auto f = reader::groupNames({run("Goldsmith", 30), run("Fran Goldsmith", 200),
                                       run("Peter Goldsmith", 8)},
                                      5);
    REQUIRE(f.size() == 2);
    const NameGroup* fran = byDisplay(f, "Fran Goldsmith");
    REQUIRE(fran != nullptr);
    CHECK(hasMember(*fran, "Goldsmith"));
    CHECK(byDisplay(f, "Peter Goldsmith") != nullptr);
  }
}

TEST_CASE("a prefix is a nickname, guarded twice") {
  SUBCASE("Stu joins Stuart") {
    const auto g = reader::groupNames({run("Stu", 500), run("Stuart", 60)}, 5);
    REQUIRE(g.size() == 1);
    CHECK(g[0].display == "Stu");
    CHECK(hasMember(g[0], "Stuart"));
  }
  SUBCASE("A PLURAL IS NOT A NICKNAME") {
    // `Noir -> Noirs` and `Etat -> Etats` were two of the first eight merges this
    // rule drew, and both are one word inflected rather than two names for one
    // person.
    const auto g = reader::groupNames({run("Noir", 40), run("Noirs", 30)}, 5);
    CHECK(g.size() == 2);
  }
  SUBCASE("an AMBIGUOUS prefix is declined unless one extension dominates 3:1") {
    // `Fran` extends to both `Frank` and `Frannie`. Merging Frank into Frannie is
    // far worse than leaving a nickname unlinked.
    const auto close = reader::groupNames({run("Fran", 100), run("Frannie", 60),
                                           run("Frank", 40)},
                                          5);
    CHECK(close.size() == 3);
    // ...and a DOMINANT extension is taken, because declining outright cost a
    // top-three entity its link on a real book.
    const auto clear = reader::groupNames({run("Fran", 100), run("Frannie", 300),
                                           run("Frank", 9)},
                                          5);
    REQUIRE(clear.size() == 2);
    const NameGroup* fr = byDisplay(clear, "Frannie");
    REQUIRE(fr != nullptr);
    CHECK(hasMember(*fr, "Fran"));
  }
  SUBCASE("a two-character stem is too little to be a nickname") {
    const auto g = reader::groupNames({run("Al", 40), run("Albert", 30)}, 5);
    CHECK(g.size() == 2);
  }
}

TEST_CASE("the furniture cut drops a running header before anything is grouped") {
  // A RUNNING HEADER IS NOT A CHARACTER, and the position finds it where the tag
  // does not. BEFORE grouping, because the book's own title folds into the character
  // sharing its name and would dilute the group's figure -- Dexter's group reads 29%
  // with the title in it and 18% without.
  const auto g = reader::groupNames(
      {run("Darkly Dreaming Dexter", 40, /*opening=*/38, /*total=*/40), run("Dexter", 300)}, 5);
  REQUIRE(g.size() == 1);
  CHECK(g[0].display == "Dexter");
  CHECK(!hasMember(g[0], "Darkly Dreaming Dexter"));

  SUBCASE("a real character who happens to open a chapter or two survives") {
    // Real characters run 0-7% chapter-opening mentions.
    const auto k = reader::groupNames({run("Dexter", 300, /*opening=*/14, /*total=*/300)}, 5);
    REQUIRE(k.size() == 1);
    CHECK(k[0].display == "Dexter");
  }
}

TEST_CASE("the list is alphabetical, which inverts the obvious order") {
  // Ranking by mentions puts the leads at the top and you never look up a lead: the
  // name you cannot place is RARE, so most-mentioned-first buries it hundreds of
  // rows down and moves it as you read on.
  const auto g = reader::groupNames({run("Zoe", 900), run("Amy", 12), run("Marc", 300)}, 5);
  REQUIRE(g.size() == 3);
  CHECK(g[0].display == "Amy");
  CHECK(g[1].display == "Marc");
  CHECK(g[2].display == "Zoe");
}

TEST_CASE("the display threshold is a different number from admission's") {
  // Admission is about what the CARD keeps so a count can go on growing; this one is
  // about how many rows a reader scrolls.
  const std::vector<NameIndexEntry> in = {run("Amy", 12), run("Bob", 4), run("Cal", 2)};
  CHECK(reader::groupNames(in, 2).size() == 3);
  CHECK(reader::groupNames(in, 5).size() == 1);
  CHECK(reader::groupNames(in, 20).empty());
}

TEST_CASE("a group with nothing longer to reveal has an EMPTY fullest form") {
  // Which is the SHORT row, and it is the common case: measured over two real novels
  // once grouping ran, rows with no fuller form are 84% and 97%. The board's first
  // draft said a third, from an estimate made before anything grouped. Repeating the
  // display name underneath would be the only thing worse than leaving it blank.
  const auto g = reader::groupNames({run("Lowick", 40), run("Mary Garth", 52)}, 5);
  REQUIRE(g.size() == 2);
  for (const auto& x : g) CHECK(x.fullest.empty());
}
