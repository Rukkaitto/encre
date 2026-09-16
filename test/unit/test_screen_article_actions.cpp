#include "doctest.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_article_actions.h"
#include "reader/theme_quiet.h"

using namespace reader;

namespace {
GestureEvent ev(Gesture g) {
  GestureEvent e;
  e.what = g;
  e.steps = 1;
  return e;
}
ArticleActionsScreen::Facts facts(bool starred = false) {
  return {7, "Why We Forget Most of the Books We Read", starred};
}
}  // namespace

TEST_CASE("the article actions overlay is built from facts, never from its parent") {
  // DeleteConfirmScreen's argument, and the reason it was rewritten: a screen
  // reference makes an overlay reachable from ONE parent. This one has two from
  // the day it lands -- the list and the end screen -- so the reference form was
  // never available to it.
  ArticleActionsScreen s(facts());
  CHECK(s.isOverlay());
  CHECK(s.id() == ScreenId::ArticleActions);
  CHECK(s.facts().id == 7);
  CHECK(s.vm().actions.size() == 2);
  CHECK(s.vm().actions[0].label == "Archive");
  CHECK(s.vm().actions[1].label == "Star");
  // Neither row opens a screen, so neither draws a chevron.
  CHECK_FALSE(s.vm().actions[0].discloses);
  CHECK_FALSE(s.vm().actions[1].discloses);
}

TEST_CASE("the second row names what it will do, not what the article is") {
  ArticleActionsScreen s(facts(/*starred=*/true));
  CHECK(s.vm().actions[1].label == "Unstar");
}

TEST_CASE("each row latches its own outcome and pops nothing") {
  ArticleActionsScreen s(facts());
  CHECK(s.chosen() == ArticleActionsScreen::Chosen::None);

  Action a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticleActionsScreen::Chosen::Archive);

  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  a = s.onGesture(ev(Gesture::Activate));
  CHECK(a.kind == Action::Kind::Article);
  CHECK(s.chosen() == ArticleActionsScreen::Chosen::Star);
}

TEST_CASE("Back is navigation, so it pops itself rather than latching") {
  // Action::wifi()'s rule: a screen latches when the shell has work to do and
  // pops itself when it has not. Routing this through the latch would be
  // machinery bought for no work.
  ArticleActionsScreen s(facts());
  const Action a = s.onGesture(ev(Gesture::Back));
  CHECK(a.kind == Action::Kind::Pop);
  CHECK(s.chosen() == ArticleActionsScreen::Chosen::None);
}

TEST_CASE("the focus wraps between the two rows") {
  ArticleActionsScreen s(facts());
  CHECK(s.focus() == 0);
  s.onGesture(ev(Gesture::Next));
  CHECK(s.focus() == 1);
  s.onGesture(ev(Gesture::Next));
  CHECK(s.focus() == 0);
  s.onGesture(ev(Gesture::Prev));
  CHECK(s.focus() == 1);
}

TEST_CASE("the panel's footprint is NOT constant, because focusing the last row moves it") {
  // ItemActions' one-pixel lesson, and this panel reaches it on the first press.
  // `rowRuleFor` drops the rule for the focused row AND for the last one, so
  // focusing the LAST row makes two suppressions coincide: the panel is a pixel
  // shorter and, being centred, a pixel lower. A constant footprint would let
  // App::renderTopOnly repaint in place across that move and leave the old top
  // border standing.
  ArticleActionsScreen s(facts());
  REQUIRE(s.focus() == 0);
  const uint32_t onFirst = s.paintFootprint();
  REQUIRE(s.onGesture(ev(Gesture::Next)).kind == Action::Kind::Redraw);
  REQUIRE(s.focus() == 1);
  CHECK(s.paintFootprint() != onFirst);
  // And neither is zero, which is Screen's "no promise" and would forbid the
  // partial repaint on both states rather than on the one that needs it.
  CHECK(onFirst != 0u);
  CHECK(s.paintFootprint() != 0u);
}

TEST_CASE("and the panel really does move, measured on the frame rather than argued") {
  // THE ARITHMETIC ABOVE IS NOT THE EVIDENCE. ItemActions' own one-pixel defect
  // was found on glass, not in a header, so this renders both focus states and
  // finds the panel's top border in each. The panel is the only full-width run
  // of ink in the middle of a veiled frame -- the veil is a 3px-grid stipple, so
  // a solid row of set bits is the border and nothing else.
  ramp::Ramp ramp;
  QuietTheme theme;

  auto panelTop = [&](int focusRow) {
    ArticleActionsScreen s(facts());
    while (s.focus() != focusRow) s.onGesture(ev(Gesture::Next));
    Framebuffer fb(480, 800);
    fb.clear(true);
    s.render(fb, ramp.fonts, theme, Plane::Bw);
    // The panel's top border is the first FULL-WIDTH RUN OF INK on the frame.
    // `getPixel` is true for PAPER here -- fb.clear(true) is a white frame -- and
    // the veil only ever ORs white dots, so nothing but the panel inks a solid
    // 340px run.
    const int x = (480 - 340) / 2;
    for (int y = 0; y < 800; ++y) {
      int run = 0;
      while (run < 340 && !fb.getPixel(x + run, y)) ++run;
      if (run == 340) return y;
    }
    return -1;
  };

  const int topOnFirst = panelTop(0);
  const int topOnLast = panelTop(1);
  REQUIRE(topOnFirst > 0);
  REQUIRE(topOnLast > 0);
  // ONE PIXEL, AND THE DIRECTION IS THE OPPOSITE OF THE OBVIOUS GUESS -- which
  // is why this is measured. With TWO rows, focusing row 0 leaves BOTH rows
  // ruleless (row 0 is focused, row 1 is last), and focusing row 1 gives row 0
  // its rule back. So the last row is the TALLER state, and a taller centred
  // panel sits HIGHER.
  CHECK(topOnLast == topOnFirst - 1);
}
