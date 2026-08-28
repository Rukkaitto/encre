// Theme::peekMetrics: the peek panel's BOX MODEL.
//
// design/Peek.dc.html states an intent and not a set of numbers -- 34px of veil either
// side at the authored 480 width, and NO height at all, because "the panel is sized by
// its text, not the other way round". So what this file pins is which of those two is
// the constant and which is derived, and how many of the reader's four typography
// fields reach a column that is not the reading column.
//
// Numbers rather than pixels, as test_theme_reader_metrics.cpp is: the goldens assert
// the paint, this asserts the box.
#include "doctest.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/layout.h"
#include "reader/settings.h"
#include "reader/components.h"
#include "reader/theme.h"
#include "reader/theme_quiet.h"
#include "reader/tracking.h"
#include "reader/viewmodel.h"

namespace {
using readerfix::Body;

// The line box PageBuilder will lay these pages on, in its own unit.
//
// `Tracking::em(font.ppem(), leadEm1000)` is what PageBuilder's constructor computes,
// and the ppem is the load-bearing half: `line-height: 1.7` on `font-size: 32px` is
// 54.4px, which is the board's own measured line box. Resolving the lead against
// `lineHeight()` instead would give 48 * 1.7 and reserve room for twelve lines while
// claiming eight -- so this helper spells the layout's arithmetic rather than a
// plausible-looking neighbour of it.
int leadF26Of(const reader::GlyphSource& body, int leadEm1000) {
  return reader::Tracking::em(body.ppem(), leadEm1000).f26();
}

// How many lines PageBuilder fits in a column this tall -- `rows_` verbatim.
int linesIn(int columnH, int leadF26) {
  return leadF26 > 0 ? reader::pxToF26(columnH) / leadF26 : 0;
}
}  // namespace

TEST_CASE("the peek's column is narrower than the reading column, at both geometries") {
  // THE FACT THAT COSTS THE PANEL ITS PAGE NUMBER. The peek is an inset panel, so its
  // measure is the PANEL's box and not the page's -- a narrower column re-wraps, and
  // re-wrapped text paginates differently, which is why the band says chapter and
  // percent instead of `53 / 890`. If these two columns were ever the same width the
  // board's whole argument would be moot, and the panel would be lying about why.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  const reader::Settings s;

  for (const int w : {480, 528}) {
    const int h = (w == 480) ? 800 : 792;
    reader::PageMetrics read, peek;
    theme.readerMetrics(w, h, ramp.fonts, body.face, s, read);
    theme.peekMetrics(w, h, ramp.fonts, body.face, s, peek);

    CAPTURE(w);
    CHECK(peek.columnW > 0);
    CHECK(peek.columnW < read.columnW);
    // And inset from the left as well as narrower -- a column of the same left edge
    // and less width would be a page with a wider right margin, not a panel.
    CHECK(peek.columnLeft > read.columnLeft);
  }
}

TEST_CASE("the peek's veil margin is 34px at the authored width and derived elsewhere") {
  // THE PANEL WIDTH IS THE CONSTANT, NOT THE VEIL MARGIN, and only one of the two can
  // be. The board is authored at 480 with `left: 34px; width: 412px`; pinning the 34
  // would make the panel 460 wide on the X3, which changes its MEASURE -- so the same
  // sentence of the same book would wrap differently on the two panels for no reason
  // the design states. Pinning the width is the call kActionsPanelW and kConfirmPanelW
  // already make, and it makes the veil margin a result: 34 at 480, 58 at 528.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  const reader::Settings s;

  reader::PageMetrics x4, x3;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, s, x4);
  theme.peekMetrics(528, 792, ramp.fonts, body.face, s, x3);

  CHECK(x4.columnW == x3.columnW);
  // ...and the panel is centred, so the whole difference in canvas width falls half
  // either side of it.
  CHECK(x3.columnLeft - x4.columnLeft == (528 - 480) / 2);

  // The authored geometry, stated once as a literal: 34px of veil, then the panel's
  // 2px border, then its 20px of padding.
  CHECK(x4.columnLeft == 34 + 2 + 20);
}

TEST_CASE("the peek's column holds exactly eight lines, and the height follows") {
  // EIGHT LINES IS THE DESIGN, and the panel's HEIGHT is what follows from it --
  // headerBandHeight() and hintBarHeight() are results in the same way.
  //
  // THE COUNT CANNOT BE ASSERTED AGAINST ITSELF. `columnH` is derived FROM kPeekLines,
  // so `linesIn(columnH) == kPeekLines` is a round trip: it pins the derivation (which
  // is worth pinning, see below) and says nothing about the NUMBER -- changing the
  // constant to 11 moves both sides together and fails nothing. What the number has to
  // answer to is the board, so the assertions that bite on it are the two geometric
  // claims the board makes: the panel does not reach the hint bar, and it is inset far
  // enough from the top to read as a modal. Eleven lines breaks both -- it is what
  // "filled the glass to within 48px of the top" measures as here.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  const reader::Settings s;

  // The peek's own bar, built exactly as renderPeek builds it: CLOSE / GO HERE and two
  // empty slots, which are 36px wide rather than zero.
  const reader::PeekViewModel vm;
  reader::Hint hints[4];
  reader::buildHints(reader::kHintSlotMarks, vm.hints, vm.holds, hints);
  const int barH = reader::hintBarHeight(ramp.fonts, hints);

  for (const int w : {480, 528}) {
    const int h = (w == 480) ? 800 : 792;
    reader::PageMetrics m;
    theme.peekMetrics(w, h, ramp.fonts, body.face, s, m);
    CAPTURE(w);

    const int leadF26 = leadF26Of(body.face, m.leadEm1000);
    // THE DERIVATION IS PageBuilder'S OWN RULE, not a division that merely looks like
    // it. A column reserved as `kPeekLines * round(lead)` is a quarter of a pixel short
    // at the default ppem and lead, and PageBuilder then fits SEVEN -- a panel that
    // claims eight lines and paginates to seven, which whole-pixel arithmetic on either
    // side cannot reveal.
    CHECK(linesIn(m.columnH, leadF26) == reader::kPeekLines);
    // The whole-pixel form the board states: 54px line boxes, eight of them.
    CHECK(m.columnH / reader::f26ToPx(leadF26) == reader::kPeekLines);

    // THE PANEL'S BOTTOM EDGE, from the column the metrics report plus the two runs
    // below it the board states -- `padding: 16px 20px 20px 20px` and the 2px border.
    // The board says outright that the bar "is drawn over the veil after this, and the
    // panel does not reach it", and a panel that did would put its own border through
    // the labels.
    const int panelBottom = m.columnTop + m.columnH + 20 + reader::kPanelBorder;
    CHECK(panelBottom <= h - barH);

    // AND IT READS AS A MODAL, which is the whole of the eight-vs-eleven argument. The
    // panel is centred, so the veil above it is what is left below it -- and the bar's
    // own height is the non-arbitrary measure of "enough": a panel inset from the top
    // by less than the bar takes at the bottom is a bordered full screen. At eight
    // lines this is 127px against the board's measured 128; at eleven it is 46.
    CHECK(h - panelBottom >= barH);
  }
}

TEST_CASE("the peek's line spacing and justification follow the reader's settings, and the "
          "margin does not") {
  // FOUR TYPOGRAPHY FIELDS, TWO READS, and the asymmetry is stated rather than left to
  // be discovered. `lineSpacing` and `justify` reach this column exactly as they reach
  // the reading one -- it is the same book at the same reading size, only the measure
  // is different. `bodyPpem` is absent for readerMetrics' reason: it has already
  // arrived, as the face. And `margins` is absent because a margin is the reading
  // PAGE's box model and this panel's box is its own, inset by a width the design
  // fixes -- there is nothing here for it to apply to.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;

  reader::Settings s;  // the defaults
  reader::PageMetrics base;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, s, base);

  SUBCASE("line spacing and alignment pass straight through") {
    s.justify = false;
    s.lineSpacing = 1600;
    reader::PageMetrics m;
    theme.peekMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK_FALSE(m.justify);
    CHECK(m.leadEm1000 == 1600);
    // ...and the column follows the lead it was given: still eight lines, on a
    // shorter box.
    CHECK(linesIn(m.columnH, leadF26Of(body.face, 1600)) == reader::kPeekLines);
    CHECK(m.columnH < base.columnH);
  }

  SUBCASE("the margin reaches the reading page and not the panel") {
    // 30 is kMarginSteps' widest -- the one value that moves the reading column most,
    // so a peek that read it could not fail to differ.
    s.margins = 30;
    reader::PageMetrics m;
    theme.peekMetrics(480, 800, ramp.fonts, body.face, s, m);
    CHECK(m.columnW == base.columnW);
    CHECK(m.columnLeft == base.columnLeft);

    // The control: the same change DOES move the reading column, so this pair states
    // a difference between two functions rather than a setting nothing reads.
    reader::PageMetrics read, readBase;
    reader::Settings d;
    theme.readerMetrics(480, 800, ramp.fonts, body.face, d, readBase);
    theme.readerMetrics(480, 800, ramp.fonts, body.face, s, read);
    CHECK(read.columnW != readBase.columnW);
  }
}

