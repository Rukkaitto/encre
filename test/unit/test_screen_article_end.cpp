#include "doctest.h"
#include "reader/screen_article_end.h"

using namespace reader;

namespace {
GestureEvent ev(Gesture g) {
  GestureEvent e;
  e.what = g;
  e.steps = 1;
  return e;
}
ArticleEndScreen::Facts facts(bool starred = false, bool hasNext = true, int left = 2) {
  return {3, "The Death and Life of the Great American Essay", "LONGREADS", 22, starred, left,
          hasNext};
}
using C = ArticleEndScreen::Chosen;
}  // namespace

TEST_CASE("four slabs, and the band counts what is left on the card") {
  ArticleEndScreen s(facts());
  CHECK(s.id() == ScreenId::ArticleEnd);
  CHECK(s.vm().title == "ARTICLE FINISHED");
  CHECK(s.vm().leftValue == "2 LEFT");
  CHECK(s.vm().actions.size() == 4);
  CHECK(s.vm().actions[0] == "ARCHIVE");
  CHECK(s.vm().actions[1] == "STAR");
  CHECK(s.vm().actions[2] == "NEXT ARTICLE");
  CHECK(s.vm().actions[3] == "BACK TO LIST");
  CHECK(s.vm().note == "SYNCS ON THE NEXT CONNECTION.");
  CHECK(s.vm().meta == "LONGREADS \xC2\xB7 22 MIN");
}

TEST_CASE("NEXT ARTICLE is ABSENT when there is none, never inert") {
  // WifiError's rule -- the slab list IS the shape -- and BookErrorMemory's
  // before it: an action that cannot work is absent, because a slab that draws
  // and does nothing is the works-only-sometimes trap.
  ArticleEndScreen s(facts(/*starred=*/false, /*hasNext=*/false));
  CHECK(s.vm().actions.size() == 3);
  CHECK(s.vm().actions[2] == "BACK TO LIST");
}

TEST_CASE("the focused slab is resolved through the list, not through a fixed index") {
  // With NEXT ARTICLE gone, BACK TO LIST is slab 2 rather than slab 3. An enum of
  // POSITIONS would name the wrong outcome in exactly the state the slab list
  // shrinks, which is the state a reader reaches at the end of their last
  // article.
  ArticleEndScreen s(facts(/*starred=*/false, /*hasNext=*/false));
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  REQUIRE(s.focus() == 2);
  CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
  CHECK(s.chosen() == C::BackToList);
}

TEST_CASE("every slab latches its own outcome") {
  ArticleEndScreen s(facts());
  const C expected[] = {C::Archive, C::Star, C::NextArticle, C::BackToList};
  for (int i = 0; i < 4; ++i) {
    while (s.focus() != i) s.onGesture(ev(Gesture::Next));
    CHECK(s.onGesture(ev(Gesture::Activate)).kind == Action::Kind::Article);
    CHECK(s.chosen() == expected[i]);
  }
}

TEST_CASE("STAR names what it will do") {
  ArticleEndScreen s(facts(/*starred=*/true));
  CHECK(s.vm().actions[1] == "UNSTAR");
}

TEST_CASE("Back returns to the last page, which is BookEnd's rule") {
  // This screen is reached by a PAGE TURN, so Back is that turn undone rather
  // than leaving the article. Leaving is what `BACK TO LIST` is for, and it
  // latches, because where the list comes back is the shell's decision.
  ArticleEndScreen s(facts());
  const Action a = s.onGesture(ev(Gesture::Back));
  CHECK(a.kind == Action::Kind::Pop);
  CHECK(s.chosen() == C::None);
}

TEST_CASE("nothing left is an EMPTY band value, not a zero") {
  // A count of nothing is not information, and drawHeaderBand's phantom gap
  // cancels for an empty value so the label still lands on the margin exactly.
  ArticleEndScreen s(facts(/*starred=*/false, /*hasNext=*/false, /*left=*/0));
  CHECK(s.vm().leftValue.empty());
}
