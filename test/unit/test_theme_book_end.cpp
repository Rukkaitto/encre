// THE END OF A BOOK as PIXELS (design/BookEnd.dc.html), which is the half
// test_screen_book_end.cpp cannot see: that file asserts what the screen SAYS, and
// this one asserts where the theme PUTS it.
//
// It deliberately does not re-derive the layout. Every assertion here is a
// structural property of the rendered frame -- a run of solid rows, a byte-for-byte
// comparison of two frames -- because a test that recomputed the theme's own
// arithmetic would pass whatever that arithmetic was, which is the shape this
// project's `make compare` default already shipped once.
//
// BOTH GEOMETRIES, always. The X4 is 48px narrower and a long run fails there first.
#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/screen_book_end.h"
#include "reader/theme_quiet.h"

using reader::Framebuffer;
using reader::Plane;
using reader::QuietTheme;

namespace {

struct Panel {
  int w, h;
  const char* name;
};
constexpr Panel kPanels[] = {{480, 800, "X4"}, {528, 792, "X3"}};

reader::BookEndScreen::Facts middlemarch(int chapters = 24) {
  reader::BookEndScreen::Facts f;
  f.bookTitle = "Middlemarch";
  f.author = "George Eliot";
  f.chapterCount = chapters;
  f.libraryBeneath = true;
  return f;
}

// A rendered frame, with the focus moved onto `focus` before the paint.
void paint(Framebuffer& fb, const reader::FontSet& fonts, QuietTheme& theme,
           const reader::BookEndScreen::Facts& facts, int focus) {
  reader::BookEndScreen s(facts);
  for (int i = 0; i < focus; ++i) {
    reader::GestureEvent g;
    g.what = reader::Gesture::Next;
    s.onGesture(g);
  }
  REQUIRE(s.vm().focusedAction == focus);
  s.render(fb, fonts, theme, Plane::Bw);
}

bool inked(const Framebuffer& fb, int x, int y) { return !fb.getPixel(x, y); }

// Whether a row is inked across the whole content column and NOT into the margin.
// That is exactly a slab: the header band's rule, the hint bar's rule and every
// full-bleed fill in this firmware start at x = 0, so they are excluded by the
// second half rather than by a list of exceptions.
bool slabRow(const Framebuffer& fb, int y) {
  if (inked(fb, reader::kMargin - 1, y)) return false;
  for (int x = reader::kMargin; x < fb.width() - reader::kMargin; ++x)
    if (!inked(fb, x, y)) return false;
  return true;
}

struct Run {
  int top, height;
  int bottom() const { return top + height - 1; }
};

// The maximal runs of slab rows, top to bottom. A FILLED slab is one run of
// kActionH; an OUTLINED one is two runs of kActionBorder, its own top and bottom
// edges -- so the runs alone say which of the two slabs the focus is on, without
// this file knowing where either sits.
std::vector<Run> slabRuns(const Framebuffer& fb) {
  std::vector<Run> runs;
  int start = -1;
  for (int y = 0; y <= fb.height(); ++y) {
    const bool s = y < fb.height() && slabRow(fb, y);
    if (s && start < 0) start = y;
    if (!s && start >= 0) {
      runs.push_back({start, y - start});
      start = -1;
    }
  }
  return runs;
}

struct Box {
  int top, bottom;
  int height() const { return bottom - top + 1; }
};

// The two slab boxes.
//
// EACH SLAB PRODUCES EXACTLY TWO RUNS, whichever variant it is, and that symmetry is
// what lets this file find both without knowing which is which. An OUTLINED slab
// draws its top and bottom borders and nothing between them. A FILLED one looks like
// it should be a single run of kActionH and is NOT: its label is reversed OUT of the
// fill (`Ink::White`, components.cpp:340), so every row crossing the label has white
// pixels in it and is not solid -- which splits the box into the band above the label
// and the band below it.
//
// This file asserted the single-run model first and failed against a correct render,
// which is the more useful direction for a test to be wrong in.
std::vector<Box> slabBoxes(const Framebuffer& fb) {
  const std::vector<Run> runs = slabRuns(fb);
  std::vector<Box> out;
  for (size_t i = 0; i + 1 < runs.size(); i += 2)
    out.push_back({runs[i].top, runs[i + 1].bottom()});
  return out;
}

bool rowsEqual(const Framebuffer& a, const Framebuffer& b, int y) {
  for (int x = 0; x < a.width(); ++x)
    if (a.getPixel(x, y) != b.getPixel(x, y)) return false;
  return true;
}

// The board's `gap: 12px` between the two slabs. The only number this file states;
// the two HEIGHTS either side of it are the primitive's and are asserted against it
// rather than against a literal.
constexpr int kSlabGap = 12;

}  // namespace

// NOTHING LEAVES THE PANEL. This screen draws no full-bleed fill of its own -- only
// the two bars' rules run to x = 0 -- so ink in a margin is a run that overran its
// box, and the X4's 432px column is where a long label finds out first.
TEST_CASE("BookEnd keeps every run inside the panel") {
  ramp::Ramp r;
  QuietTheme theme;
  for (const Panel& p : kPanels) {
    CAPTURE(p.w);
    for (int focus = 0; focus < reader::BookEndScreen::kRowCount; ++focus) {
      CAPTURE(focus);
      Framebuffer fb(p.w, p.h);
      paint(fb, r.fonts, theme, middlemarch(), focus);

      int ruleRows = 0;
      for (int y = 0; y < fb.height(); ++y) {
        // A full-bleed row is one of the two rules and is the only thing allowed
        // to ink column 0.
        bool fullBleed = true;
        for (int x = 0; x < fb.width(); ++x)
          if (!inked(fb, x, y)) {
            fullBleed = false;
            break;
          }
        if (fullBleed) {
          ++ruleRows;
          continue;
        }
        for (int x = 0; x < reader::kMargin; ++x) {
          CAPTURE(y);
          CAPTURE(x);
          CHECK_FALSE(inked(fb, x, y));
          CHECK_FALSE(inked(fb, fb.width() - 1 - x, y));
        }
      }
      // The band's 2px rule and the hint bar's 1px one -- so the loop above was
      // not silently skipping every row as full-bleed.
      CHECK(ruleRows == 3);
      // And something was actually drawn.
      CHECK(slabRuns(fb).size() >= 2u);
    }
  }
}

// THE TWO SLABS OCCUPY THE SAME RECT, whichever the focus is on. `box-sizing:
// border-box` on the outlined variant is what makes that true in the primitive; a
// caller that advanced by a pinned height instead of by what drawActionButton
// returned would break it here rather than on glass.
TEST_CASE("BookEnd's slabs hold their rect across a focus move") {
  ramp::Ramp r;
  QuietTheme theme;
  for (const Panel& p : kPanels) {
    CAPTURE(p.w);
    Framebuffer a(p.w, p.h), b(p.w, p.h);
    paint(a, r.fonts, theme, middlemarch(), reader::BookEndScreen::kFinish);
    paint(b, r.fonts, theme, middlemarch(), reader::BookEndScreen::kLeave);

    // Both frames hold two slab boxes of kActionH, in the same place. The BOX is
    // what must not move: `box-sizing: border-box` means a filled slab and an
    // outlined one occupy exactly the same rect, so a focus move between them can
    // shift nothing -- and a caller that advanced by a pinned 72 rather than by what
    // drawActionButton returned would fail the height and the gap together.
    const std::vector<Box> ba = slabBoxes(a);
    const std::vector<Box> bb = slabBoxes(b);
    REQUIRE(ba.size() == 2u);
    REQUIRE(bb.size() == 2u);

    CHECK(ba[0].height() == reader::kActionH);
    CHECK(ba[1].height() == reader::kActionH);
    CHECK(bb[0].height() == reader::kActionH);
    CHECK(bb[1].height() == reader::kActionH);

    CHECK(ba[0].top == bb[0].top);
    CHECK(ba[0].bottom == bb[0].bottom);
    CHECK(ba[1].top == bb[1].top);
    CHECK(ba[1].bottom == bb[1].bottom);

    // And the board's `gap: 12px` between them, which is what a pinned advance moves.
    CHECK(ba[1].top - ba[0].bottom - 1 == kSlabGap);

    // THE VARIANTS REALLY DID SWAP, which the geometry above cannot see: a render
    // that drew both slabs the same way would satisfy every assertion so far. The
    // middle row of each box is solid black when filled and paper-with-two-edges
    // when outlined, so it must DIFFER between the two frames.
    CHECK_FALSE(rowsEqual(a, b, ba[0].top + reader::kActionH / 2));
    CHECK_FALSE(rowsEqual(a, b, ba[1].top + reader::kActionH / 2));

    // EVERYTHING BELOW THE SLABS IS BYTE-IDENTICAL: the note hangs off the bottom
    // and the hint bar draws itself there, so neither can know which slab the
    // focus is on.
    for (int y = ba[1].bottom + 1; y < p.h; ++y) {
      CAPTURE(y);
      CHECK(rowsEqual(a, b, y));
    }
  }
}

// THE META LINE IS ABSENT, NOT BLANK, when the spine count is unknown -- so the
// content block above the slabs is shorter and the slabs rise. The NOTE must not:
// the board gives it `margin-top: auto`, so it hangs off the bottom and not off the
// slabs, and that is the whole reason it stays put here.
TEST_CASE("BookEnd's note and hint bar do not follow the missing meta line") {
  ramp::Ramp r;
  QuietTheme theme;
  for (const Panel& p : kPanels) {
    CAPTURE(p.w);
    Framebuffer with(p.w, p.h), without(p.w, p.h);
    paint(with, r.fonts, theme, middlemarch(24), reader::BookEndScreen::kFinish);
    paint(without, r.fonts, theme, middlemarch(0), reader::BookEndScreen::kFinish);

    const std::vector<Box> bw = slabBoxes(with);
    REQUIRE(bw.size() == 2u);
    const int slabsBottom = bw[1].bottom;

    // The block above legitimately differs -- that is the meta line going away --
    // and the slabs move with it, so the fixture is reaching the code under test.
    bool movedAbove = false;
    for (int y = 0; y <= slabsBottom; ++y)
      if (!rowsEqual(with, without, y)) movedAbove = true;
    REQUIRE(movedAbove);

    for (int y = slabsBottom + 1; y < p.h; ++y) {
      CAPTURE(y);
      CHECK(rowsEqual(with, without, y));
    }
  }
}
