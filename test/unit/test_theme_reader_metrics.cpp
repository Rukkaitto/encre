// Theme::readerMetrics: the reader's BOX MODEL, and the three typography
// settings that move it.
//
// The box model is the theme's (spec 3.3) and the content is the screen's, so
// this file asserts numbers rather than pixels -- the goldens next door assert
// the pixels. What it exists to pin is that the DEFAULTS are the board's own
// (480 less 18px each side is design/Reader.dc.html's 444) and that a margin
// change moves the column's WIDTH and never its HEIGHT.
#include "doctest.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/layout.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"

namespace {
using readerfix::Body;
}  // namespace

TEST_CASE("readerMetrics follows the typography settings") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;

  reader::Settings s;  // the defaults
  reader::PageMetrics base;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, s, base);

  // THE DEFAULTS ARE THE BOARD'S. 480 less 18px each side is 444, which is the
  // number design/Reader.dc.html states and every reader golden is laid at. These
  // two are LITERALS deliberately: they pin the board's own numbers, which is a
  // claim worth stating in one place.
  CHECK(base.columnLeft == 18);
  CHECK(base.columnW == 444);
  // AND THESE TWO ARE ASSERTED AGAINST THE INPUT rather than against the layout
  // constants they happen to equal, so they state the PASS-THROUGH instead of the
  // coincidence -- `settings.cpp` static_asserts lineSpacing's default equal to
  // kBodyLeadEm, so `base.leadEm1000 == kBodyLeadEm` would be a claim about that
  // assert and not about this function.
  //
  // THEY STILL CANNOT BITE HERE, AND THAT IS STRUCTURAL RATHER THAN A GAP: at the
  // DEFAULT input an implementation that reads the settings and one that ignores
  // them are equal by construction, which is exactly what "every default is
  // today's behaviour to the pixel" means and is why no golden moved. A mutation
  // replacing the body with the old hardcoded constants fails six assertions, all
  // of them in the SUBCASES below -- so the subcases are where the bite is, and
  // this block's job is to name the board's numbers.
  CHECK(base.leadEm1000 == s.lineSpacing);
  CHECK(base.justify == s.justify);

  SUBCASE("wider margins narrow the column from both sides") {
    s.margins = 30;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnLeft == 30);
    CHECK(m.columnW == 480 - 2 * 30);
  }
  SUBCASE("tighter margins widen it") {
    s.margins = 10;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnLeft == 10);
    CHECK(m.columnW == 480 - 2 * 10);
  }
  SUBCASE("line spacing and alignment pass straight through") {
    s.lineSpacing = 2000;
    s.justify = false;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.leadEm1000 == 2000);
    CHECK_FALSE(m.justify);
  }
  SUBCASE("the column's HEIGHT does not depend on the margins") {
    // The band and the footer are full-bleed on the board -- their padding is
    // kReadPadX but their HEIGHT is type -- so a margin change must not move the
    // column's top or shorten it. Getting this wrong loses a line of every page
    // at one margin setting and nothing at another, which is the hardest kind of
    // layout bug to attribute.
    s.margins = 30;
    reader::PageMetrics m;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnTop == base.columnTop);
    CHECK(m.columnH == base.columnH);
  }
}
