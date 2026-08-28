// Theme::renderTypography's BOX MODEL: the preview box's derived height, and how
// many specimen lines it holds.
//
// The box model is the theme's (spec 3.3) and the copy is the screen's, so this
// file asserts geometry read back off the framebuffer rather than pixels -- the
// golden next door will assert the pixels. What it exists to pin is the number
// design/Typography.dc.html is sized around: FOUR whole lines of specimen at the
// default setting, at BOTH geometries, and a box that does not change height when
// the settings or the focus do.
//
// THE BOX'S HEIGHT IS MEASURED OFF THE BOARD, NOT COPIED FROM THE PLAN -- see
// kBoxH_X4 below, which names what Chrome actually lays out and why the plan's
// figure is a footnote line short of it.
//
// The specimen here is filler rather than the board's sentence on purpose: whether
// four lines FIT is a fact about the box and the lead, and tying it to the copy
// would make a wording change look like a layout regression.
#include <iterator>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/screen_typography.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

namespace {

// The body face at an arbitrary ppem, with its bytes beside it: a ScalableFont
// borrows the buffer it was initialised from and never copies it. readerfix::Body
// is pinned to kBodyPpem, and half of what this file checks is what happens at the
// TOP of the size ramp.
struct BodyAt {
  std::vector<uint8_t> bytes =
      golden::slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
  reader::ScalableFont face;
  explicit BodyAt(int ppem) {
    REQUIRE(face.init(bytes.data(), bytes.size(), ppem));
    REQUIRE(face.ready());
  }
};

// Enough words that the wrap always overflows the box, whatever the size: what is
// being measured is how many lines fit, not how long the copy is.
const char* kFiller =
    "Miss Brooke had that kind of beauty which seems to be thrown into relief by "
    "poor dress, and her hand and wrist were so finely formed that she could wear "
    "sleeves not less bare of style than those in which the Blessed Virgin "
    "appeared to Italian painters, and her profile as well as her stature and "
    "bearing seemed to gain the more dignity from her plain garments.";

reader::TypographyViewModel vmAt(int leadEm1000) {
  reader::TypographyViewModel vm;
  vm.title = "TYPOGRAPHY";
  vm.specimen = kFiller;
  vm.leadEm1000 = leadEm1000;
  const char* labels[5] = {"Font", "Size", "Margins", "Line spacing", "Alignment"};
  const char* values[5] = {"LITERATA", "15 PT", "COMFORTABLE", "1.7", "JUSTIFIED"};
  for (int i = 0; i < 5; ++i) {
    reader::ListRow r;
    r.label = labels[i];
    r.value = values[i];
    r.focusable = i != 0;
    vm.rows.push_back(r);
  }
  vm.focusedRow = 1;  // Size, as the board draws it
  vm.hints = {"BACK", "CHANGE", "UP", "DOWN"};
  return vm;
}

// Whether row `y` has any ink strictly inside the preview box's border columns.
bool inkInside(const reader::Framebuffer& fb, int y) {
  for (int x = reader::kMargin + 2; x < fb.width() - reader::kMargin - 2; ++x)
    if (!fb.getPixel(x, y)) return true;
  return false;
}

// Whether row `y` is inked all the way across those columns -- a horizontal rule
// rather than a line of text.
bool solidInside(const reader::Framebuffer& fb, int y) {
  for (int x = reader::kMargin + 2; x < fb.width() - reader::kMargin - 2; ++x)
    if (fb.getPixel(x, y)) return false;
  return true;
}

// The preview box, found in the frame rather than recomputed from the theme's own
// private arithmetic -- which would make this a test of one expression against
// itself. It is the only box on this screen whose horizontal rules are INSET: the
// header band's border, the rows block's border, every row rule and the focused
// row's fill are all full-bleed, so `x == 0` is white for the box and inked for
// each of those.
struct Box {
  int top = -1, bottom = -1;
  int height() const { return bottom - top + 1; }
};

Box findBox(const reader::Framebuffer& fb) {
  Box b;
  for (int y = 0; y < fb.height(); ++y) {
    if (!solidInside(fb, y)) continue;
    if (!fb.getPixel(0, y)) continue;  // full-bleed: not the box
    if (b.top < 0) b.top = y;
    b.bottom = y;
  }
  return b;
}

// Maximal runs of rows carrying ink between the box's borders: one per drawn line
// of specimen.
int specimenLines(const reader::Framebuffer& fb, const Box& box) {
  int bands = 0;
  bool in = false;
  for (int y = box.top + 2; y <= box.bottom - 2; ++y) {
    const bool ink = inkInside(fb, y);
    if (ink && !in) ++bands;
    in = ink;
  }
  return bands;
}

// design/Typography.dc.html's `padding: 12px 24px` on the preview box -- the
// VERTICAL half, which no setting moves. Named here because the ink-free band below
// is derived from it rather than from a 12. The HORIZONTAL half is the Margins
// setting now and is deliberately not restated here: the margin case below asks
// which pixels moved rather than where the padding is, so it cannot fall out of
// step with the theme's arithmetic.
constexpr int kTypoPreviewPadY = 12;
constexpr int kTypoPreviewBorder = 2;  // its `border: 2px`

const std::pair<int, int> kGeometries[] = {{480, 800}, {528, 792}};

// THE BOX'S OUTER HEIGHT, and these two numbers were MEASURED OFF THE RENDERED
// BOARD rather than taken from the plan, which states 250 and 241.
//
// design/Typography.dc.html renders its preview box at **282px on the X4 and 274
// on the X3** -- read out of Chrome's own layout, per element, at both frame
// overrides compare-design.py applies. The plan's 250/241 is exactly one 31.5px
// footnote line short of that, so it was measured while the footnote wrapped to
// THREE lines; the copy is two lines now (confirmed in the same measurement) and
// the box grew by the line the footnote gave back.
//
// The firmware derives 280 and 272, 2px under the board, and both pixels are
// accounted for rather than tolerated:
//
//   - 1px is the FOCUSED ROW'S RULE. The board draws a focused row, which drops
//     its `border-bottom` (rowRuleFor's rule), so its rows block measures 255
//     where this theme measures 256 -- deliberately, because measuring the drawn
//     height would make the box's height depend on WHICH row has the focus, and
//     the whole point of deriving it is that the five rows never move.
//   - 1px is two half-pixel line boxes: Chrome's `LIVE PREVIEW` block is 44.5 and
//     its hint bar 63.5, where the firmware's whole-pixel line heights make them
//     45 and 64. That is the ordinary Chrome-versus-firmware difference this
//     project's fidelity numbers are made of.
constexpr int kBoxH_X4 = 280;
constexpr int kBoxH_X3 = 272;

int wantBoxH(int panelW) { return panelW == 480 ? kBoxH_X4 : kBoxH_X3; }

}  // namespace

TEST_CASE("the board's own specimen wraps to the board's four lines") {
  // THE COPY, not filler -- the one case in this file that is about the specimen
  // rather than about the box. design/Typography.dc.html wraps this sentence to
  // exactly FOUR lines in Chrome at both frame sizes (measured per line rect: the
  // fourth is 168px of 396 on the X4 and 90px of 444 on the X3), and the firmware's
  // whole-pixel advances measure ~3% wider -- which is the margin a board's copy
  // has to be checked in BOTH engines for. SdMissing's paragraph needed its
  // max-width taken 400 -> 420 for exactly this.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  BodyAt body(reader::kBodyPpem);
  reader::TypographyViewModel vm = vmAt(1700);
  vm.specimen = reader::TypographyScreen::kSpecimen;
  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    reader::Framebuffer fb(geo.first, geo.second);
    theme.renderTypography(fb, ramp.fonts, &body.face, vm, reader::Plane::Bw);
    const Box box = findBox(fb);
    REQUIRE(box.top > 0);
    CHECK(specimenLines(fb, box) == 4);
  }
}

TEST_CASE("the preview box holds four whole lines at the default setting") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  BodyAt body(reader::kBodyPpem);
  const reader::TypographyViewModel vm = vmAt(1700);

  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    reader::Framebuffer fb(geo.first, geo.second);
    theme.renderTypography(fb, ramp.fonts, &body.face, vm, reader::Plane::Bw);

    const Box box = findBox(fb);
    REQUIRE(box.top > 0);
    // Derived, not pinned -- a pinned 292 was tried on the board and was wrong by
    // 42px, which flex-shrink hid. See kBoxH_X4 for the board's own measured
    // numbers and for where the 2px between them and these goes.
    CHECK(box.height() == wantBoxH(geo.first));
    // FOUR, at BOTH geometries, which is the whole point of the file: the board's
    // own specimen wraps to exactly four lines in Chrome at both frame sizes, so
    // four is the count the box is sized around. Here the copy is filler long
    // enough to overflow, so this is the CLAMP landing on four rather than the
    // wrap happening to stop there -- which is the stronger of the two claims.
    CHECK(specimenLines(fb, box) == 4);
  }
}

TEST_CASE("no drawn line's ink leaves the preview box") {
  // The property the clamp exists for, and the one a `ceil` would break: a line
  // whose ink overflows would be SLICED by the box's own border, which is the
  // defect design/Reader.dc.html's column once had.
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  struct Case {
    int ppem, lead;
  };
  // The bottom, the default and the top of the offered ramp, at the tightest and
  // the loosest lead -- so the count is exercised over the whole space the screen
  // can reach rather than at one setting.
  const Case cases[] = {{25, 1400}, {32, 1700}, {46, 2000}, {46, 1400}, {25, 2000}};
  for (const Case& c : cases) {
    CAPTURE(c.ppem);
    CAPTURE(c.lead);
    BodyAt body(c.ppem);
    const reader::TypographyViewModel vm = vmAt(c.lead);
    for (const auto& geo : kGeometries) {
      CAPTURE(geo.first);
      reader::Framebuffer fb(geo.first, geo.second);
      theme.renderTypography(fb, ramp.fonts, &body.face, vm, reader::Plane::Bw);
      const Box box = findBox(fb);
      REQUIRE(box.top > 0);
      // At least one line is always drawn -- the box is sized to hold the biggest
      // offered face -- and the rows below the box never move, so its height is
      // the same at every setting.
      CHECK(specimenLines(fb, box) >= 1);
      CHECK(box.height() == wantBoxH(geo.first));
      // THE WHOLE BOTTOM PADDING BAND IS INK-FREE, not just the two rows against
      // the border. Checking two was the first version of this and it could not
      // see a slice: an overrun of a few pixels lands in the middle of the 12px
      // padding, clear of the border and clear of the rows being checked.
      for (int y = box.bottom - 2 - kTypoPreviewPadY + 1; y <= box.bottom - 2; ++y)
        CHECK_FALSE(inkInside(fb, y));
      // The TOP band is checked at two rows only, and deliberately: at a lead
      // tighter than the face's own extent a line box is shorter than the ink in
      // it, so the first line legitimately reaches up into the padding -- which is
      // what Chrome does too. Only the bottom edge is a slice.
      for (int y = box.top + 2; y < box.top + 4; ++y) CHECK_FALSE(inkInside(fb, y));
    }
  }
}

TEST_CASE("a lead tighter than the face's own extent still never slices a line") {
  // The property extended past what CHANGE can reach. Every lead on
  // kLineSpacingSteps is LOOSER than the face's extent, so a line's ink sits inside
  // its own line box there and `floor(boxH / lead)` gives the same answer the ink
  // test does -- a mutation to floor passes this whole file, which is said plainly
  // in previewLinesThatFit rather than implied away here. Below the extent the ink
  // hangs out of its box, which is where the two rules start to differ.
  //
  // 1000 is NOT on kLineSpacingSteps and cannot be reached by pressing CHANGE. It
  // is here because the theme's contract is about the lead it is HANDED, and a
  // tighter step is one line in settings.h away.
  //
  // IT DOES NOT BITE THE floor MUTATION EITHER, and that is measured rather than
  // hoped: the two rules disagree only when the box's height lands in a window
  // ~(extent - lead)/2 px wide inside a line box, and neither panel's box does at
  // this lead. Tuning a lead until it did would be a test of the implementation
  // against itself.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  BodyAt body(reader::kBodyPpem);
  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    reader::Framebuffer fb(geo.first, geo.second);
    theme.renderTypography(fb, ramp.fonts, &body.face, vmAt(1000), reader::Plane::Bw);
    const Box box = findBox(fb);
    REQUIRE(box.top > 0);
    CHECK(box.height() == wantBoxH(geo.first));
    CHECK(specimenLines(fb, box) >= 1);
    for (int y = box.bottom - 2 - kTypoPreviewPadY + 1; y <= box.bottom - 2; ++y)
      CHECK_FALSE(inkInside(fb, y));
  }
}

TEST_CASE("a bigger face or a looser lead holds fewer lines") {
  // The clamp has to be a function of the SETTINGS and not a constant, which a
  // count that never changes would hide.
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  auto linesFor = [&](int ppem, int lead, int w, int h) {
    BodyAt body(ppem);
    reader::Framebuffer fb(w, h);
    theme.renderTypography(fb, ramp.fonts, &body.face, vmAt(lead), reader::Plane::Bw);
    const Box box = findBox(fb);
    REQUIRE(box.top > 0);
    return specimenLines(fb, box);
  };

  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    const int base = linesFor(32, 1700, geo.first, geo.second);
    CHECK(base == 4);
    // The SIZE, at one lead.
    CHECK(linesFor(46, 1700, geo.first, geo.second) < base);
    CHECK(linesFor(25, 1700, geo.first, geo.second) > base);
    // And the LEAD, at one size -- compared against the same face's own tightest
    // setting rather than against `base`, because one step of lead at ppem 32 does
    // NOT drop a line on either panel (64px boxes still land four inside a 254px
    // content area) and asserting that it does would have been a false claim about
    // the clamp dressed up as a passing test.
    CHECK(linesFor(46, 2000, geo.first, geo.second) <
          linesFor(46, 1400, geo.first, geo.second));
  }
}

TEST_CASE("the preview follows the Alignment row, and RAGGED is not JUSTIFIED") {
  // THE BOX SAYS `LIVE PREVIEW`. Without this the `Alignment` row spends a ~520 ms
  // repaint changing four characters of its own value while the box does not move,
  // which is a preview visibly ignoring one of its four rows.
  //
  // Two frames against each other rather than a golden, because that is the only
  // question a drawing OPTION can be tested by: a golden blessed from the render
  // that ignored the flag passes forever.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  BodyAt body(reader::kBodyPpem);

  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    reader::TypographyViewModel just = vmAt(1700);
    just.justify = true;
    reader::TypographyViewModel rag = vmAt(1700);
    rag.justify = false;
    rag.rows[4].value = "RAGGED";  // what the screen's own label would say

    reader::Framebuffer a(geo.first, geo.second), b(geo.first, geo.second);
    theme.renderTypography(a, ramp.fonts, &body.face, just, reader::Plane::Bw);
    theme.renderTypography(b, ramp.fonts, &body.face, rag, reader::Plane::Bw);
    CHECK_FALSE(golden::identical(a, b));

    // AND THE DIFFERENCE IS IN THE PREVIEW BOX, not merely in the row value that
    // was also changed -- otherwise this would pass with a preview that ignores the
    // flag entirely, which is exactly the defect it exists to catch.
    const Box box = findBox(a);
    REQUIRE(box.top > 0);
    CHECK_FALSE(golden::rowsIdentical(a, b, box.top, box.bottom + 1));

    // The box still holds the same four whole lines: alignment sets a line, it does
    // not move a break, so the clamp cannot change with it.
    CHECK(specimenLines(a, box) == specimenLines(b, findBox(b)));
  }
}

TEST_CASE("the preview follows the Margins row, and the border does not move") {
  // THE ROW THIS SCREEN SHIPPED IGNORING. Reported off the device as "changing the
  // margins doesn't update the live preview" -- the same defect the Alignment case
  // above exists for, on the row the spec had explicitly excluded.
  //
  // TWO HALVES, AND THE SECOND IS THE ONE THAT BITES. That the wrap moves is easy;
  // that the BORDER does not is the property a fix which tracked the setting with
  // the outline instead of the padding would break, and it would break it
  // invisibly -- the box would still be one box, and every row under it would step
  // on every press of one row. So the box is FOUND in each frame and its edges
  // compared, rather than its height alone: a border drawn 8px narrower has the
  // same height and is a different box.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  BodyAt body(reader::kBodyPpem);

  // The ends of kMarginSteps: 10 draws the board's old 16px of padding and 30 draws
  // 36, so the measure differs by 40px -- more than enough for the wrap to move on
  // either panel.
  const int tightest = reader::kMarginSteps[0];
  const int widest = reader::kMarginSteps[std::size(reader::kMarginSteps) - 1];

  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    reader::TypographyViewModel tight = vmAt(1700);
    tight.margins = tightest;
    tight.specimen = reader::TypographyScreen::kSpecimen;
    reader::TypographyViewModel wide = vmAt(1700);
    wide.margins = widest;
    wide.specimen = reader::TypographyScreen::kSpecimen;

    reader::Framebuffer a(geo.first, geo.second), b(geo.first, geo.second);
    theme.renderTypography(a, ramp.fonts, &body.face, tight, reader::Plane::Bw);
    theme.renderTypography(b, ramp.fonts, &body.face, wide, reader::Plane::Bw);

    // THE WRAP MOVED. Asserted inside the box rather than over the whole frame, so
    // it cannot pass on some other pixel having changed -- the row values are
    // identical in these two models, but a future field would not be.
    const Box boxA = findBox(a);
    const Box boxB = findBox(b);
    REQUIRE(boxA.top > 0);
    CHECK_FALSE(golden::rowsIdentical(a, b, boxA.top, boxA.bottom + 1));

    // AND THE BORDER DID NOT. Same top row, same bottom row, and the same inked
    // columns on the top border -- which is what says the outline is where it was
    // and only the measure inside it changed.
    CHECK(boxA.top == boxB.top);
    CHECK(boxA.bottom == boxB.bottom);
    CHECK(boxA.height() == wantBoxH(geo.first));
    CHECK(boxB.height() == wantBoxH(geo.first));
    for (int y = boxA.top; y < boxA.top + kTypoPreviewBorder; ++y)
      for (int x = 0; x < a.width(); ++x) CHECK(a.getPixel(x, y) == b.getPixel(x, y));
    // The rows block, LIVE PREVIEW, the footnote and the hint bar are all below the
    // box, and none of them may move: that is the whole reason the padding carries
    // this setting and the border does not.
    CHECK(golden::rowsIdentical(a, b, boxA.bottom + 1, a.height()));

    // No line is sliced at the narrower measure either -- the wrap got longer, so
    // the clamp has to hold at both.
    for (int y = boxB.bottom - 2 - kTypoPreviewPadY + 1; y <= boxB.bottom - 2; ++y)
      CHECK_FALSE(inkInside(b, y));
  }
}

TEST_CASE("a null body face draws the box and no specimen") {
  // A supported state, not an oversight: the tests and a Settings-only build have
  // no body face, and an absent preview is not an absent screen.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  for (const auto& geo : kGeometries) {
    CAPTURE(geo.first);
    reader::Framebuffer fb(geo.first, geo.second);
    theme.renderTypography(fb, ramp.fonts, nullptr, vmAt(1700), reader::Plane::Bw);
    const Box box = findBox(fb);
    REQUIRE(box.top > 0);
    CHECK(box.height() == wantBoxH(geo.first));
    CHECK(specimenLines(fb, box) == 0);
  }
}
