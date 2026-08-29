// THE PEEK'S GOLDENS, and they are four-level rather than 1-bit.
//
// The screen declares Fidelity::Grayscale for the Reader's reason -- this is body text
// at reading size, and hard-thresholding a serif face at 32px was judged worse on the
// panel -- so a Mono golden here would pin the wrong thing convincingly.
//
// AND THE VEIL IS IN THEM, which is what makes these the visual half of
// test_veil_planes.cpp: that file proves the veil's bytes are the same in every plane,
// and these prove the composed four-level image of a veiled page under a panel is the
// one that was approved.
#include <memory>
#include <string>
#include <utility>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/app.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/layout.h"
#include "reader/screen_peek.h"
#include "reader/screen_reader.h"
#include "reader/screens.h"
#include "reader/settings.h"
#include "reader/theme.h"
#include "reader/theme_quiet.h"
#include "reader/tracking.h"
#include "reader/viewmodel.h"

namespace {
using readerfix::Body;
}  // namespace

TEST_CASE("QuietTheme renders the peek over a page to golden on both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;

  auto renderOne = [&](int w, int h, const std::string& name) {
    // TWO SETS OF METRICS, WHICH IS THE WHOLE FEATURE. readerMetrics places the
    // reading page's ~444px column and peekMetrics places the panel's ~368px one --
    // inset, so narrower, so the same prose re-wraps. A fixture that handed the peek
    // the reading measure would build a panel that agrees with the page underneath,
    // which is the one state this design exists to refuse.
    reader::PageMetrics rm, pm;
    theme.readerMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, rm);
    theme.peekMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, pm);

    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body.face);
    factory.setReaderMetrics(rm);
    factory.setReaderDemo();
    factory.setPeekMetrics(pm);
    factory.setPeekDemo();

    std::unique_ptr<reader::Screen> page = factory.create(reader::ScreenId::Reader);
    REQUIRE(page != nullptr);
    // THE SETTLED STATE, which is what the board shows: a chapter opens with its total
    // unknown and the count arrives with the four-level refinement, within five
    // seconds. Rendering before that would pin the veiled page's footer as `1 / —`,
    // the em dash a chapter wears for its first moments, where the board draws a real
    // counter.
    static_cast<reader::ReaderScreen*>(page.get())->completeIndex();

    reader::App app(std::move(page), factory);
    REQUIRE(app.pushScreen(reader::ScreenId::Peek));
    REQUIRE(app.top().fidelity() == reader::Fidelity::Grayscale);

    // THROUGH `app.render`, NEVER `top().render`. An overlay's parent is painted by
    // App::render walking down to the topmost non-overlay; calling this screen alone
    // paints a panel floating on white, and NOTHING on the desktop can catch that --
    // the simulator and every other golden go through App::render, so they would all
    // pass while the device was wrong. It has happened once.
    reader::Framebuffer lsb(w, h), msb(w, h);
    app.render(lsb, ramp.fonts, theme, reader::Plane::Lsb);
    app.render(msb, ramp.fonts, theme, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "peek"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "peek_x3"); }
}

namespace {

// THE PANEL'S BOTTOM BORDER, FOUND IN THE DRAWN FRAME rather than recomputed. The
// point of the assertions below is to compare the render's box against the metrics'
// box, so taking the border from anything but the pixels would compare peekBox with
// itself.
//
// It is the LAST row carrying a long solid run: the panel is `kPeekPanelW` wide and
// its 2px border is the only full-width black run down there. The band's rule is four
// pixels narrower and lies above it, so scanning from the bottom picks the border; the
// hint bar's own rule spans the whole canvas, which is why the run is bounded above as
// well as below.
int panelBottomBorderY(const reader::Framebuffer& fb) {
  int found = -1;
  for (int y = 0; y < fb.height(); ++y) {
    int run = 0, best = 0;
    for (int x = 0; x < fb.width(); ++x) {
      run = fb.getPixel(x, y) ? 0 : run + 1;  // getPixel: true is paper
      if (run > best) best = run;
    }
    if (best >= 400 && best <= 440) found = y;
  }
  return found;
}

}  // namespace

TEST_CASE("the peek's panel holds whole lines and does not reach the hint bar") {
  // THE BOARD'S CLAIMS, ASSERTED BESIDE THE GOLDEN RATHER THAN ONLY INSIDE IT. A
  // golden pins the pixels and cannot say WHY they are those pixels: re-blessed after
  // a change that clipped a descender it would go on passing forever.
  //
  // "A PAGE OF TEXT ENDS ON A WHOLE LINE" is the board's own sentence, and it is there
  // because its first draft pinned a 560px height with `overflow: hidden` and cut the
  // last line in half lengthwise -- which reads as a rendering fault rather than as a
  // page boundary.
  //
  // THE FIRST OF THE TWO CHECKS BELOW IS NEARLY A ROUND TRIP AND IS LABELLED AS ONE.
  // `columnH` is the fixed panel less its band and padding, and PageBuilder fits
  // `columnH / lead` boxes into it -- the same rowsThatFit the theme derives its own
  // count with -- so text laid by the metrics cannot leave the column it reported
  // whatever the face's descent is -- it bites only if the FACE's nominal extent
  // exceeds the LEAD.
  //
  // AND THIS SCREEN DOES REACH THAT CASE, which the comment here used to deny ("not the
  // hazard this screen has") on the strength of a walk that only ever ran at the
  // default lead. settings.h records the two tightest steps of kLineSpacingSteps as
  // deliberately tighter than the face's own ink, and walking the ramp below found it:
  // at ppem 46 and lead 1.000 the first line's nominal top is 12px above the column.
  // The bound is split accordingly at the assertion, which is the honest form of a
  // check that was only ever exercised where it could not fail.
  //
  // WHAT DEFENDS IT IS THE SECOND: the text placed by peekMetrics against the border
  // drawn by renderPeek, in PIXELS. Those are two functions computing one box, which is
  // this project's first invariant.
  //
  // IT USED TO BE THE READER'S LEAD THAT COULD MAKE THEM DISAGREE, and it no longer
  // can: the panel is a fixed box now (theme.h's kPeekPanelH), so neither function
  // takes a lead and PeekViewModel no longer carries one. The mutation this comment
  // named -- "deleting the `vm.leadEm1000` the render is given fails it" -- is not
  // writable any more, because the field it deleted does not exist. What replaces it is
  // WIDER coverage rather than a sharper single case: the walk below runs the real
  // pagination at both ENDS of both typography ramps, where the derived line count is
  // 17 and 4 rather than 8, and asserts the ink stays inside a panel that did not move.
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  // The default, then the four corners of (kBodyPpemSteps x kLineSpacingSteps) -- the
  // extremes are what the old derivation could not survive, and they are where a page
  // laid against the wrong box would overflow by the most.
  const std::pair<int, int> settingsUnderTest[] = {
      {reader::Settings{}.bodyPpem, reader::Settings{}.lineSpacing},
      {25, 1000}, {25, 2000}, {46, 1000}, {46, 2000},
  };
  for (const auto& ps : settingsUnderTest) {
    for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
      const int w = geo.first, h = geo.second;
      const int ppem = ps.first, lead = ps.second;
      CAPTURE(w);
      CAPTURE(ppem);
      CAPTURE(lead);
      reader::Settings s;
      s.bodyPpem = ppem;
      s.lineSpacing = lead;
      Body body(ppem);
      reader::PageMetrics rm, pm;
      theme.readerMetrics(w, h, ramp.fonts, body.face, s, rm);
      theme.peekMetrics(w, h, ramp.fonts, body.face, s, pm);

      reader::DemoScreenFactory factory;
      factory.setReaderBody(&body.face);
      factory.setReaderMetrics(rm);
      factory.setReaderDemo();
      factory.setPeekMetrics(pm);
      factory.setPeekDemo();
      std::unique_ptr<reader::Screen> page = factory.create(reader::ScreenId::Reader);
      REQUIRE(page != nullptr);
      reader::App app(std::move(page), factory);
      REQUIRE(app.pushScreen(reader::ScreenId::Peek));
      const auto& peek = static_cast<const reader::PeekScreen&>(app.top());

      const reader::Page& p = peek.page();
      // A PANEL WITH NOTHING IN IT IS THE FAILURE THIS GUARDS. Everything below is
      // vacuously true over an empty page, which is exactly the shape of the "reports
      // on less than it claims" defect this repo keeps hitting.
      REQUIRE_FALSE(p.lines.empty());
      // A CEILING, AND IT IS ONE ON PURPOSE. What this case is about is where the ink
      // lands against the drawn border, which is true of a page holding fewer lines
      // than the panel offers. The count itself is pinned to the NUMBER in
      // test_screen_peek.cpp, over a page in the body of a real chapter -- every check
      // on this panel's line count was `<=` until then, and a ceiling is satisfied by
      // seven.
      CHECK(static_cast<int>(p.lines.size()) <=
            theme.peekVisibleLines(ramp.fonts, body.face, s));

      // The LAST line is the only one that can fall out of the box: the lines are laid
      // on an ascending run of line boxes, so if the deepest descender is inside the
      // column every line above it is too.
      const int inkBottom = p.lines.back().baselineY - body.face.descent();  // descent < 0
      CAPTURE(p.lines.back().baselineY);
      CAPTURE(body.face.descent());
      CHECK(inkBottom <= pm.columnTop + pm.columnH);

      // AND THE FIRST LINE'S TOP, WHICH IS NOT THE SAME BOUND AT EVERY LEAD -- this is
      // the hazard settings.h records rather than one this panel introduced. The face's
      // nominal extent is `ascent - descent`, and the two tightest steps of
      // kLineSpacingSteps are deliberately TIGHTER than it: at ppem 32 the extent is
      // 48px against a 32px box at lead 1.000 and 38px at 1.200. That was offered
      // anyway, as a reading-comfort call to be settled on the glass.
      //
      // So a first line's NOMINAL top rises above the column at those two steps -- by
      // 12px at ppem 46 and 1.000, measured. What must still hold there is that it stays
      // out of the BAND, which is what a reader would see as damage: the body's 16px of
      // top padding is the room it has. Where the extent fits its box, the column's own
      // top is the bound.
      const int nominalExtent = body.face.ascent() - body.face.descent();
      const int lineBoxPx = reader::f26ToPx(reader::Tracking::em(ppem, lead).f26());
      const int inkTop = p.lines.front().baselineY - body.face.ascent();
      CAPTURE(nominalExtent);
      CAPTURE(lineBoxPx);
      if (nominalExtent <= lineBoxPx) {
        CHECK(inkTop >= pm.columnTop);
      } else {
        // 16 is the board's `padding: 16px 20px 20px 20px`, top side.
        CHECK(inkTop >= pm.columnTop - 16);
      }

      // THE DRAWN BORDER, AND THE TEXT INSIDE IT. Bw rather than a grayscale plane
      // because a border is coverage 0 or 3 and is therefore identical in every pass --
      // it is partial coverage that a plane changes.
      reader::Framebuffer fb(w, h);
      app.render(fb, ramp.fonts, theme, reader::Plane::Bw);
      const int border = panelBottomBorderY(fb);
      REQUIRE(border > 0);
      CHECK(inkBottom < border);

      // AND THE PANEL DOES NOT REACH THE HINT BAR. test_theme_peek_metrics.cpp makes
      // this claim about the box peekMetrics computes; this makes it about the border
      // that was actually drawn, which is the half a caller could get wrong.
      const reader::PeekViewModel& vm = peek.vm();
      reader::Hint hints[4];
      reader::buildHints(reader::kHintSlotMarks, vm.hints, vm.holds, hints);
      CHECK(border < h - reader::hintBarHeight(ramp.fonts, hints));
    }
  }
}
