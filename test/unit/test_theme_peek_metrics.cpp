// Theme::peekMetrics: the peek panel's BOX MODEL.
//
// design/Peek.dc.html states two numbers and derives everything else from them: a
// 412px panel, 546px tall, centred both ways. So what this file pins is which parts of
// the box are constant and which are derived, and how many of the reader's four
// typography fields reach a column that is not the reading column.
//
// THE BOARD SAID THE OPPOSITE FOR A PHASE -- "no height here, the content sets it" --
// and the panel's height was `ceil(kPeekLines * lineBox) + 110`. That made the panel a
// different size on every reader's device: ~310px for one running a small ppem at a
// tight lead, and 846px at the top of both ramps, which does not fit either glass. See
// theme.h's kPeekPanelH for the two measurements, and the two cases below for the
// properties they became.
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

// THE HELPER THAT USED TO LIVE HERE IS GONE, AND THAT IS THE POINT OF THE CHANGE.
// `linesIn()` was PageBuilder's `rows_ = pxToF26(columnH) / leadF26_` TRANSCRIBED, and
// its own comment said what was wrong with it: "change the derivation and this file
// stays green while the panel paginates to seven". Both sides call reader::rowsThatFit
// now -- one spelling, in layout.h, used by PageBuilder's constructor and by the
// theme's peekVisibleLines alike.

// The peek's panel, reconstructed from the column the metrics report plus the runs
// either side of it the board states: `padding: 16px 20px 20px 20px`, the band, and the
// 2px border. Spelled here as literals for the reason the veil margin is -- they are
// the board's authored geometry, and peekBox is not public, so a derivation that
// drifted from the box the theme actually computes shows up as a panel that is no
// longer centred.
struct PanelEdges {
  int top = 0, bottom = 0;
};
PanelEdges panelEdgesOf(const reader::PageMetrics& m, const reader::FontSet& fonts) {
  const reader::Font& lbl = fonts[reader::Role::Label500];
  const reader::Font& val = fonts[reader::Role::Value700];
  // The band is `align-items: center`, so its line box is the TALLER of its two faces;
  // then `padding: 18px 20px` and the 2px rule under it.
  const int bandH =
      (val.lineHeight() > lbl.lineHeight() ? val.lineHeight() : lbl.lineHeight()) + 2 * 18 + 2;
  return {m.columnTop - 16 - bandH - reader::kPanelBorder,
          m.columnTop + m.columnH + 20 + reader::kPanelBorder};
}

// The peek's own bar, built exactly as renderPeek builds it: CLOSE / GO HERE and two
// empty slots, which are 36px wide rather than zero.
int peekBarH(const reader::FontSet& fonts) {
  const reader::PeekViewModel vm;
  reader::Hint hints[4];
  reader::buildHints(reader::kHintSlotMarks, vm.hints, vm.holds, hints);
  return reader::hintBarHeight(fonts, hints);
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

TEST_CASE("the peek's box is fixed and its LINE COUNT is what varies") {
  // THE INVERSION, ASSERTED OVER THE WHOLE RAMP. Until this changed, `kPeekLines = 8`
  // was the input and the panel's height was the result -- so the box moved with the
  // reader's typography, and the two things it moved into were both defects:
  //
  //   * ON GLASS THE PANEL WAS "A LOT SHORTER" THAN THE SIMULATOR SHOWS. Reported by a
  //     reader at a smaller ppem and a tighter lead. Eight of THEIR line boxes is
  //     ~310px against 546 -- a small box adrift in a lot of veil, on a screen whose
  //     job is to read as a modal. Nothing on the desktop could show it, because
  //     nothing renders the reader at non-default typography (#40).
  //   * AT THE TOP OF THE RAMP THE PANEL WAS TALLER THAN THE GLASS. See the regression
  //     case below, which states that arithmetic.
  //
  // So what this walks is every combination of the two ramps that reach this panel, at
  // both geometries, and it asserts that the BOX does not move and the COUNT does.
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    const int w = geo.first, h = geo.second;
    const int barH = peekBarH(ramp.fonts);

    // The default's box, which every other step has to reproduce exactly.
    readerfix::Body base(reader::Settings{}.bodyPpem);
    reader::PageMetrics ref;
    theme.peekMetrics(w, h, ramp.fonts, base.face, reader::Settings{}, ref);
    const PanelEdges refEdges = panelEdgesOf(ref, ramp.fonts);

    // Distinct counts seen across the walk. 32 slots covers the ramp's widest answer
    // (17 lines, at ppem 25 and lead 1.000) with room for a step added below it.
    bool seen[32] = {};
    int distinct = 0;

    for (const int ppem : reader::kBodyPpemSteps) {
      for (const int lead : reader::kLineSpacingSteps) {
        CAPTURE(w);
        CAPTURE(ppem);
        CAPTURE(lead);
        reader::Settings s;
        s.bodyPpem = ppem;
        s.lineSpacing = lead;
        readerfix::Body body(ppem);
        reader::PageMetrics m;
        theme.peekMetrics(w, h, ramp.fonts, body.face, s, m);

        // THE BOX DOES NOT MOVE -- every field of it, not just the height, because a
        // panel that kept its height and slid up the screen would be the same defect
        // wearing different numbers.
        CHECK(m.columnLeft == ref.columnLeft);
        CHECK(m.columnTop == ref.columnTop);
        CHECK(m.columnW == ref.columnW);
        CHECK(m.columnH == ref.columnH);
        const PanelEdges e = panelEdgesOf(m, ramp.fonts);
        CHECK(e.top == refEdges.top);
        CHECK(e.bottom == refEdges.bottom);

        // AND IT IS ON THE SCREEN, which is the half the old derivation lost at the top
        // of the ramp. Stated against the hint bar rather than against 0 and `h`,
        // because the board's two geometric claims are that the panel does not reach
        // the bar and is inset far enough from the top to read as a modal.
        CHECK(e.top >= barH);
        CHECK(e.bottom <= h - barH);

        // THE COUNT IS AT LEAST ONE. This is what the clamp in peekLineCount is for,
        // and this walk is what says the clamp is dead code rather than load-bearing:
        // the widest line box either ramp asks for is 46 * 2.000 = 92px against a 436px
        // column.
        const int n = theme.peekVisibleLines(ramp.fonts, body.face, s);
        CHECK(n >= 1);
        // ...and every one of those lines is a WHOLE line box inside the column, which
        // is the half of the board's argument that survived the inversion: "a page of
        // text ends on a whole line".
        CHECK(n * reader::Tracking::em(ppem, lead).f26() <= reader::pxToF26(m.columnH));
        // ...and one more would not fit, so the count is the largest honest one rather
        // than merely a safe one. A panel showing seven lines where eight fit is 54px
        // of unexplained white.
        CHECK((n + 1) * reader::Tracking::em(ppem, lead).f26() > reader::pxToF26(m.columnH));

        REQUIRE(n < 32);
        if (!seen[n]) {
          seen[n] = true;
          ++distinct;
        }
      }
    }

    // THE COUNT REALLY DOES VARY, and this is what keeps the walk above from being 70
    // copies of one assertion. Without it a peekVisibleLines that returned a constant
    // would satisfy every check in this case.
    CHECK(distinct >= 3);
  }
}

TEST_CASE("the peek's panel fits the glass at the top of the settings ramp") {
  // THE REGRESSION FOR THE SECOND FINDING, stated as its own case because the
  // arithmetic is the whole point and a walk hides it.
  //
  // UNDER THE OLD DERIVATION THIS FAILED. `columnH` was `ceil(8 * lineBox)` and the
  // panel was `110 + columnH`; at kBodyPpemSteps' 46 and kLineSpacingSteps' 2000 the
  // line box is 46 * 2.0 = 92px, so the column was 736 and the panel 846 -- against 800
  // on the X4 and 792 on the X3. centreIn(0, 800, 846) is (800 - 846) / 2 = -23, so the
  // panel began 23px ABOVE the top of the screen and ran 23px past the bottom of it,
  // with its border off the glass at both ends.
  //
  // The old code is not kept around to demonstrate that; the numbers above are, which
  // is this project's rule about a mutation you cannot run.
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  reader::Settings s;
  s.bodyPpem = reader::kBodyPpemSteps[sizeof(reader::kBodyPpemSteps) / sizeof(int) - 1];
  s.lineSpacing =
      reader::kLineSpacingSteps[sizeof(reader::kLineSpacingSteps) / sizeof(int) - 1];
  CHECK(s.bodyPpem == 46);
  CHECK(s.lineSpacing == 2000);
  readerfix::Body body(s.bodyPpem);

  for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    const int w = geo.first, h = geo.second;
    CAPTURE(w);
    reader::PageMetrics m;
    theme.peekMetrics(w, h, ramp.fonts, body.face, s, m);
    const PanelEdges e = panelEdgesOf(m, ramp.fonts);

    // The panel is 546 tall wherever it is drawn, and it is on the glass.
    CHECK(e.bottom - e.top == reader::kPeekPanelH);
    CHECK(e.top > 0);
    CHECK(e.bottom < h);
    // AND THE LINE BOX REALLY IS THE 92px THE ARITHMETIC ABOVE RESTS ON, so the case
    // cannot go on passing over a ramp whose top step moved.
    CHECK(reader::f26ToPx(reader::Tracking::em(s.bodyPpem, s.lineSpacing).f26()) == 92);
    // Four lines of it, where the old rule insisted on eight and paid 846px for them.
    CHECK(theme.peekVisibleLines(ramp.fonts, body.face, s) == 4);
  }
}

TEST_CASE("the peek's box is centred, and the odd pixel of veil goes above it") {
  // ...AND THE TWO EDGES ARE ONE FACT, because the panel is centred on the screen
  // (`top: 50%; transform: translateY(-50%)`). Off by at most a pixel, since an odd
  // amount of leftover veil cannot be halved -- and centreIn "halves up", so the odd
  // pixel goes ABOVE the panel rather than below it. That direction is asserted rather
  // than absorbed into a symmetric bound because it is the one this project chose on
  // purpose, and because it is what caught this assertion being written the other way
  // round.
  //
  // THIS PAIR IS ALSO WHAT PROVES THE PANEL RECONSTRUCTION ABOVE. panelEdgesOf derives
  // the two edges from the board's runs and peekBox is not public -- so a derivation
  // that drifted from the box the theme actually computes shows up here as a panel that
  // is no longer centred.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  const reader::Settings s;

  for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    const int w = geo.first, h = geo.second;
    CAPTURE(w);
    reader::PageMetrics m;
    theme.peekMetrics(w, h, ramp.fonts, body.face, s, m);
    const PanelEdges e = panelEdgesOf(m, ramp.fonts);
    CHECK(e.top - (h - e.bottom) >= 0);
    CHECK(e.top - (h - e.bottom) <= 1);
  }
}

TEST_CASE("the peek's panel is 546px, which is what the derived height was at the default") {
  // WHAT MAKES THIS A RE-DERIVATION AND NOT A REDESIGN. `kPeekPanelH` is not a number
  // somebody liked: it is what `2*border + band + 16 + ceil(8 * 54.4) + 20` produced at
  // the shipped settings, to the pixel. Neither peek golden moves because of it.
  //
  // The band is a RESULT of the type ramp, so the 436px column is `546 - 110` only for
  // as long as Value700 is 25px -- which is why this asserts the whole sum rather than
  // the column alone.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  const reader::Settings s;

  reader::PageMetrics m;
  theme.peekMetrics(480, 800, ramp.fonts, body.face, s, m);
  const PanelEdges e = panelEdgesOf(m, ramp.fonts);
  CHECK(e.bottom - e.top == 546);
  CHECK(reader::kPeekPanelH == 546);
  CHECK(m.columnH == 436);
  // Eight lines at the default, which is the number the board is authored at and the
  // number both peek goldens hold.
  CHECK(theme.peekVisibleLines(ramp.fonts, body.face, s) == 8);
  // ...and the old derivation's own arithmetic, spelled out: ceil(8 * 54.390625) = 436.
  CHECK((8 * reader::Tracking::em(s.bodyPpem, s.lineSpacing).f26() + 63) / 64 == 436);
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
    // ...AND THE COLUMN DOES NOT FOLLOW IT, which is the inversion at its sharpest.
    // This slot used to say "still eight lines, on a shorter box" and assert
    // `m.columnH < base.columnH`. The box is the constant now: a tighter lead buys
    // MORE lines in the same column, not the same lines in a smaller panel.
    CHECK(m.columnH == base.columnH);

    // AND 1600 IS NOT ENOUGH TO BUY ONE, which is worth stating rather than working
    // around: the default's eight boxes leave ~0.5 of a box of slack in the 436px
    // column, so tightening the lead from 1.700 to 1.600 spends the slack and fits the
    // same eight. This assertion was written as `> default` first and failed at
    // `8 > 8` -- which is a fact about the input, not about the rule.
    CHECK(theme.peekVisibleLines(ramp.fonts, body.face, s) ==
          theme.peekVisibleLines(ramp.fonts, body.face, reader::Settings{}));

    // The step below it does buy one. 1.400 on a 32px face is a 44.8px box, and 436
    // holds nine of those.
    reader::Settings tight = s;
    tight.lineSpacing = 1400;
    CHECK(theme.peekVisibleLines(ramp.fonts, body.face, tight) == 9);
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

