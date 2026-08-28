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
  // `columnH` is `ceil(kPeekLines * lead)` and PageBuilder fits `columnH / lead` boxes
  // into it, so text laid by the metrics cannot leave the column the metrics reported
  // whatever the face's descent is -- it bites only if the FACE's nominal extent
  // exceeds the LEAD, which is a real hazard (settings.h records that at
  // `lineSpacing = 1000` the face's 48px extent overflows a 32px box by ~8px) and is
  // not the hazard this screen has. It is kept because it is the board's literal
  // sentence and costs two lines; it is not what defends the claim.
  //
  // WHAT DEFENDS IT IS THE SECOND: the text placed by peekMetrics against the border
  // drawn by renderPeek, in PIXELS. Those are two functions computing one box, which
  // is this project's first invariant and the reason PeekViewModel carries the lead at
  // all -- and it is checked AT A NON-DEFAULT LINE SPACING, because at the default the
  // two agree by accident of both reaching for the same constant. Deleting the
  // `vm.leadEm1000` the render is given fails it; see the report.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;

  // Two leads: the shipped default, and the step table's widest -- which moves the
  // panel's height by 76px and is what makes the two boxes able to disagree.
  for (const int lead : {reader::Settings{}.lineSpacing, 2000}) {
    for (const auto geo : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
      const int w = geo.first, h = geo.second;
      CAPTURE(w);
      CAPTURE(lead);
      reader::Settings s;
      s.lineSpacing = lead;
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
      CHECK(static_cast<int>(p.lines.size()) <= reader::kPeekLines);

      // The LAST line is the only one that can fall out of the box: the lines are laid
      // on an ascending run of line boxes, so if the deepest descender is inside the
      // column every line above it is too.
      const int inkBottom = p.lines.back().baselineY - body.face.descent();  // descent < 0
      CAPTURE(p.lines.back().baselineY);
      CAPTURE(body.face.descent());
      CHECK(inkBottom <= pm.columnTop + pm.columnH);
      CHECK(p.lines.front().baselineY - body.face.ascent() >= pm.columnTop);

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
