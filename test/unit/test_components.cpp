#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/components.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/text.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

struct Fixture {
  std::vector<uint8_t> a, b, c, d, e, g;
  reader::FontSet fonts;
  Fixture() {
    const std::string dir = std::string(ASSETS_DIR) + "/built/";
    a = slurp(dir + "spacegrotesk_400_10pt.rfnt");
    b = slurp(dir + "spacegrotesk_500_11pt.rfnt");
    c = slurp(dir + "spacegrotesk_700_12pt.rfnt");
    d = slurp(dir + "spacegrotesk_500_14pt.rfnt");
    e = slurp(dir + "spacegrotesk_700_20pt.rfnt");
    g = slurp(dir + "spacegrotesk_700_32pt.rfnt");
    fonts.load(reader::Role::Meta, a.data(), a.size());
    fonts.load(reader::Role::Label, b.data(), b.size());
    fonts.load(reader::Role::Value, c.data(), c.size());
    fonts.load(reader::Role::Body, d.data(), d.size());
    fonts.load(reader::Role::Title, e.data(), e.size());
    fonts.load(reader::Role::Display, g.data(), g.size());
    REQUIRE(fonts.ready());
  }
};

TEST_CASE("the header band right-aligns its value on any canvas width") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    const int h = reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%");
    CHECK(h > 0);
    // Ink must reach close to the right margin, and never past it. The scan
    // stops above the band's 2px rule: the rule is deliberately full-bleed (the
    // design canvas has `border-bottom: 2px solid` spanning the whole width),
    // so including its rows would only ever report width - 1 and would say
    // nothing about where the value landed.
    int rightmost = -1;
    for (int y = 0; y < h - 2; ++y)
      for (int x = 0; x < width; ++x)
        if (!fb.getPixel(x, y) && x > rightmost) rightmost = x;
    CHECK(rightmost <= width - reader::kMargin);
    CHECK(rightmost > width - reader::kMargin - 60);
    // The rule itself spans edge to edge, on either width.
    CHECK_FALSE(fb.getPixel(0, h - 1));
    CHECK_FALSE(fb.getPixel(width - 1, h - 1));
  }
}

TEST_CASE("a focused row inverts: black field, white text") {
  Fixture f;
  reader::Framebuffer fb(480, 120);
  reader::drawRow(fb, f.fonts, 0, "LIBRARY", "12", /*focused=*/true);
  // The row's field is black...
  CHECK_FALSE(fb.getPixel(2, 10));
  // ...and contains white glyph pixels.
  bool anyWhite = false;
  for (int y = 0; y < 56 && !anyWhite; ++y)
    for (int x = 0; x < 480 && !anyWhite; ++x)
      if (fb.getPixel(x, y)) anyWhite = true;
  CHECK(anyWhite);
}

// Rightmost inked column in rows [y0, y1), or -1 when that band is blank.
static int rightmostInk(const reader::Framebuffer& fb, int y0, int y1) {
  int r = -1;
  for (int y = y0; y < y1; ++y)
    for (int x = fb.width() - 1; x > r; --x)
      if (!fb.getPixel(x, y)) r = x;
  return r;
}

TEST_CASE("the header band keeps value plus battery glyph inside the right margin") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    const int h = reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%");
    // Scan above the full-bleed 2px rule, as the alignment test above does.
    const int rightmost = rightmostInk(fb, 0, h - 2);
    // The group is right-aligned on the icon's box, so the last inked column is
    // the battery's terminal nub. Not the box's very last column, though: the
    // design's own SVG puts the nub at x=19.5..21.5 of a 22-unit viewBox, so
    // rasterised at 38px the final column carries ~14% coverage and quantises
    // to nothing. A range, not an equality -- what matters is that the mark
    // reaches the margin and never crosses it, which no exact column can say
    // without being re-derived every time the icon's scale changes.
    CHECK(rightmost <= width - reader::kMargin - 1);
    CHECK(rightmost >= width - reader::kMargin - 3);
    // The battery is a distinct mark, not just the value: its outline's left
    // edge is a full column of ink 22px in from the margin.
    const int iconX = width - reader::kMargin - reader::icons::kBattery.w;
    int outlineRows = 0;
    for (int y = 0; y < h - 2; ++y)
      if (!fb.getPixel(iconX, y)) ++outlineRows;
    CHECK(outlineRows == reader::icons::kBattery.h);
    // The value sits immediately left of the glyph rather than under it or
    // stranded mid-band: a small gap, the board's 7px, separates the two.
    int valueRight = -1;
    for (int y = 0; y < h - 2; ++y)
      for (int x = 0; x < iconX; ++x)
        if (!fb.getPixel(x, y) && x > valueRight) valueRight = x;
    CHECK(valueRight < iconX);
    CHECK(iconX - valueRight <= 10);
  }
}

TEST_CASE("a row can carry a trailing icon, a value, or neither") {
  Fixture f;
  const int width = 480;
  const int edge = width - reader::kMargin;

  SUBCASE("trailing icon inks near the right margin and never past it") {
    reader::Framebuffer fb(width, 120);
    reader::drawRow(fb, f.fonts, 0, "SETTINGS", "", /*focused=*/false, &reader::icons::kChevron);
    // Skip the hairline at y == 0.
    const int rightmost = rightmostInk(fb, 1, reader::kRowH);
    CHECK(rightmost < edge);
    CHECK(rightmost >= edge - reader::icons::kChevron.w);
  }

  SUBCASE("a focused row draws its trailing icon in white") {
    reader::Framebuffer fb(width, 120);
    reader::drawRow(fb, f.fonts, 0, "SETTINGS", "", /*focused=*/true, &reader::icons::kChevron);
    bool anyWhite = false;
    for (int y = 0; y < reader::kRowH && !anyWhite; ++y)
      for (int x = edge - reader::icons::kChevron.w; x < edge && !anyWhite; ++x)
        if (fb.getPixel(x, y)) anyWhite = true;
    CHECK(anyWhite);
  }

  SUBCASE("no value and no trailing icon leaves the right half empty") {
    reader::Framebuffer fb(width, 120);
    reader::drawRow(fb, f.fonts, 0, "SETTINGS", "", /*focused=*/false, nullptr);
    CHECK(rightmostInk(fb, 1, reader::kRowH) < width / 2);
  }

  SUBCASE("a value still right-aligns when no trailing icon is given") {
    reader::Framebuffer fb(width, 120);
    reader::drawRow(fb, f.fonts, 0, "LIBRARY", "12", /*focused=*/false);
    const int rightmost = rightmostInk(fb, 1, reader::kRowH);
    CHECK(rightmost < edge);
    CHECK(rightmost > edge - 20);
  }
}

// Lowest and highest ink row inside [x0, x1), ignoring the bar's top rule.
struct Rows {
  int top = 9999, bottom = -1;
};
static Rows inkRows(const reader::Framebuffer& fb, int barTop, int x0, int x1) {
  Rows r;
  for (int y = barTop + 1; y < fb.height(); ++y)
    for (int x = x0; x < x1; ++x)
      if (!fb.getPixel(x, y)) {
        if (y < r.top) r.top = y;
        if (y > r.bottom) r.bottom = y;
      }
  return r;
}

TEST_CASE("a hold line stays inside the bar wherever its slot sits") {
  Fixture f;
  // The real bars carry the hold on the Confirm slot, not the first one, so the
  // vertical placement cannot be decided by looking at slot 0 alone.
  for (int holdSlot : {0, 1, 2, 3}) {
    reader::Framebuffer fb(480, 120);
    reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", ""},
                             {&reader::icons::kDot, "OPEN", ""},
                             {&reader::icons::kUp, "UP", ""},
                             {&reader::icons::kDown, "DOWN", ""}};
    hints[holdSlot].hold = "HOLD: Q";  // Q descends below the baseline
    int slotX[4] = {};
    const int barH = reader::drawHintBar(fb, f.fonts, hints, slotX);
    const int barTop = fb.height() - barH;
    const Rows all = inkRows(fb, barTop, 0, 480);
    // Nothing may reach the last row: that is where a clipped descender lands.
    CHECK(all.bottom < fb.height() - 1);
    CHECK(all.top > barTop);
    // The two-line slot straddles the bar's centre rather than hanging below it.
    const int slotEnd = holdSlot < 3 ? slotX[holdSlot + 1] : 480;
    const Rows held = inkRows(fb, barTop, slotX[holdSlot], slotEnd);
    const int barCentre = barTop + barH / 2;
    CHECK(held.top < barCentre);
    CHECK(held.bottom > barCentre);
  }
}

TEST_CASE("a hold line in one slot does not move the other slots") {
  Fixture f;
  reader::Hint plain[4] = {{&reader::icons::kBack, "BACK", ""},
                           {&reader::icons::kDot, "OPEN", ""},
                           {&reader::icons::kUp, "UP", ""},
                           {&reader::icons::kDown, "DOWN", ""}};
  reader::Framebuffer noHold(480, 120);
  int ax[4] = {};
  const int barH = reader::drawHintBar(noHold, f.fonts, plain, ax);
  const int barTop = noHold.height() - barH;
  const Rows before = inkRows(noHold, barTop, ax[2], ax[3]);

  reader::Hint withHold[4] = {plain[0], plain[1], plain[2], plain[3]};
  withHold[0].hold = "HOLD";
  reader::Framebuffer held(480, 120);
  int bx[4] = {};
  reader::drawHintBar(held, f.fonts, withHold, bx);
  const Rows after = inkRows(held, barTop, bx[2], bx[3]);

  CHECK(after.top == before.top);
  CHECK(after.bottom == before.bottom);
}

TEST_CASE("structural drawing is identical in every plane") {
  Fixture f;

  // drawRow: the hairline at row 0 sits well above any glyph the label or
  // value could ever reach, so it must be bit-identical across all three
  // planes -- a plane bug here would show up as furniture damage, not
  // fringing.
  {
    auto render = [&](reader::Plane plane) {
      reader::Framebuffer fb(480, 200);
      reader::drawRow(fb, f.fonts, 0, "LIBRARY", "12", /*focused=*/false, nullptr, plane);
      return fb;
    };
    const reader::Framebuffer bw = render(reader::Plane::Bw);
    const reader::Framebuffer lsb = render(reader::Plane::Lsb);
    const reader::Framebuffer msb = render(reader::Plane::Msb);
    for (int x = 0; x < 480; ++x) {
      CHECK(bw.getPixel(x, 0) == lsb.getPixel(x, 0));
      CHECK(bw.getPixel(x, 0) == msb.getPixel(x, 0));
    }
  }

  // drawHeaderBand: the 2px full-bleed rule at the band's bottom edge is the
  // same kind of opaque furniture.
  {
    auto render = [&](reader::Plane plane) {
      reader::Framebuffer fb(480, 200);
      reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%", plane);
      return fb;
    };
    const reader::Framebuffer bw = render(reader::Plane::Bw);
    const reader::Framebuffer lsb = render(reader::Plane::Lsb);
    const reader::Framebuffer msb = render(reader::Plane::Msb);
    for (int x = 0; x < 480; ++x)
      for (int y : {reader::kBandH - 2, reader::kBandH - 1}) {
        CHECK(bw.getPixel(x, y) == lsb.getPixel(x, y));
        CHECK(bw.getPixel(x, y) == msb.getPixel(x, y));
      }
  }

  // drawHintBar: the top rule, drawn before any icon or label, is furniture
  // too.
  {
    auto render = [&](reader::Plane plane) {
      reader::Framebuffer fb(480, 120);
      const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", ""},
                                     {&reader::icons::kDot, "OPEN", ""},
                                     {&reader::icons::kUp, "UP", ""},
                                     {&reader::icons::kDown, "DOWN", ""}};
      int slotX[4] = {};
      reader::drawHintBar(fb, f.fonts, hints, slotX, plane);
      return fb;
    };
    const reader::Framebuffer bw = render(reader::Plane::Bw);
    const reader::Framebuffer lsb = render(reader::Plane::Lsb);
    const reader::Framebuffer msb = render(reader::Plane::Msb);
    const int top = bw.height() - reader::kHintBarH;
    for (int x = 0; x < 480; ++x) {
      CHECK(bw.getPixel(x, top) == lsb.getPixel(x, top));
      CHECK(bw.getPixel(x, top) == msb.getPixel(x, top));
    }
  }
}

// --- Vertical centring, and icons aligned to it -----------------------------

// Inked rows of a horizontal band, restricted to columns [x0, x1).
static Rows inkRowsIn(const reader::Framebuffer& fb, int y0, int y1, int x0, int x1) {
  Rows r;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
      if (!fb.getPixel(x, y)) {
        if (y < r.top) r.top = y;
        if (y > r.bottom) r.bottom = y;
      }
  return r;
}
// Doubled, so a half-pixel centre stays exact instead of rounding.
static int centre2(const Rows& r) { return r.top + r.bottom; }

// How far an icon's own ink sits from the centre of its own box, doubled.
//
// It is not always zero, and that is the design's business rather than a bug in
// the placement: the boards' up and down arrows are drawn from paths whose ink
// lands a pixel high and a pixel low of their 25px box respectively. Alignment
// assertions below subtract this out, so they measure where the primitive *put
// the box* -- which is what the primitive promises -- instead of also measuring
// Chrome's sub-pixel rasterisation of one arrowhead.
static int iconInkOffset2(const reader::Icon& icon) {
  int top = icon.h, bottom = -1;
  for (int y = 0; y < icon.h; ++y)
    for (int x = 0; x < icon.w; ++x)
      if (reader::coverage(icon, x, y) > 0) {
        if (y < top) top = y;
        if (y > bottom) bottom = y;
      }
  return (top + bottom) - (icon.h - 1);
}

TEST_CASE("baselineIn centres the em box, and sits higher than centring the ascent") {
  Fixture f;
  for (reader::Role role : {reader::Role::Meta, reader::Role::Label, reader::Role::Value,
                            reader::Role::Body, reader::Role::Title, reader::Role::Display}) {
    const reader::Font& font = f.fonts[role];
    for (int boxTop : {0, 7, 240}) {
      for (int boxH : {64, 72, 80, 100}) {
        CAPTURE(boxTop);
        CAPTURE(boxH);
        const int base = reader::baselineIn(font, boxTop, boxH);
        // The ascent..descent extent is centred: the slack above the run equals
        // the slack below it, to within the odd pixel integer division cannot
        // split. The slack may be *negative* -- the boards tighten some line
        // boxes below the face's own extent (a 42px title at line-height 1.05,
        // a 67px numeral at 1) and CSS lets the run overhang symmetrically
        // rather than clipping it. Asserting non-negative slack here would be
        // asserting that those boxes are illegal, and they are the design's.
        const int above = (base - font.ascent()) - boxTop;
        const int below = (boxTop + boxH) - (base - font.descent());
        CHECK(above - below >= -1);
        CHECK(above - below <= 1);
        // The regression pin. `boxTop + boxH / 2 + ascent / 2` is the formula
        // this replaced; it reads like centring but centres the ascent, and
        // ascent reserves accent room above the caps. It is lower by half the
        // descent, on every face and every box, which is why every label on the
        // screen sat low in its box.
        CHECK(base < boxTop + boxH / 2 + font.ascent() / 2);
      }
    }
  }
}

TEST_CASE("iconTopFor is the exact inverse of baselineIn") {
  Fixture f;
  // An icon placed with iconTopFor and text placed with baselineIn out of the
  // same box must share a centre for *any* icon height -- that is the whole
  // point, since the design's bars mix a 25px square mark with a 38x21 battery.
  for (reader::Role role : {reader::Role::Meta, reader::Role::Label, reader::Role::Value}) {
    const reader::Font& font = f.fonts[role];
    for (int boxH : {64, 72, 80}) {
      const int base = reader::baselineIn(font, 0, boxH);
      for (int iconH : {9, 12, 21, 25, 39, 46}) {
        CAPTURE(iconH);
        const int top = reader::iconTopFor(font, base, iconH);
        // Doubled centres, so an odd icon height needs no rounding fudge.
        const int iconCentre2 = 2 * top + iconH;
        const int boxCentre2 = boxH;
        CHECK(iconCentre2 >= boxCentre2 - 2);
        CHECK(iconCentre2 <= boxCentre2 + 2);
      }
    }
  }
}

TEST_CASE("a row's label is optically centred in the row") {
  Fixture f;
  reader::Framebuffer fb(480, 200);
  reader::drawRow(fb, f.fonts, 0, "LIBRARY", "", /*focused=*/false);
  // Left of centre is the label and nothing else; skip the hairline at y == 0.
  const Rows ink = inkRowsIn(fb, 1, reader::kRowH, reader::kMargin, 240);
  REQUIRE(ink.bottom > 0);
  // Caps sit a hair above the box's middle -- the box centres the em, and the
  // descent below the baseline is empty for a word with no descender -- so this
  // is a tolerance rather than an equality. Two pixels is tight enough to fail
  // the ascent-centred formula, which put this ink 3px low.
  const int boxCentre2 = reader::kRowH;
  CHECK(centre2(ink) >= boxCentre2 - 4);
  CHECK(centre2(ink) <= boxCentre2 + 4);
}

TEST_CASE("the header band's battery is aligned with its percentage") {
  Fixture f;
  const int width = 480;
  reader::Framebuffer fb(width, 200);
  reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%");
  const int iconX = width - reader::kMargin - reader::icons::kBattery.w;
  // Above the 2px rule, so the full-bleed rule cannot dominate either band.
  const Rows bat = inkRowsIn(fb, 0, reader::kBandH - 2, iconX, width);
  const Rows value = inkRowsIn(fb, 0, reader::kBandH - 2, width / 2, iconX);
  REQUIRE(bat.bottom > 0);
  REQUIRE(value.bottom > 0);
  // The battery is 21px tall against a 25px mark elsewhere in the same design,
  // so this can only hold if the placement is derived from the text rather than
  // offset from the baseline by a constant. Caps have no descender, so the box
  // sits a pixel below the ink it is centred on -- hence 3 doubled, not 0. The
  // formula this replaced was off by 3 *pixels*.
  const int boxCentre2 = centre2(bat) - iconInkOffset2(reader::icons::kBattery);
  CHECK(boxCentre2 >= centre2(value) - 3);
  CHECK(boxCentre2 <= centre2(value) + 3);
}

TEST_CASE("every hint mark is aligned with the label it labels") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBook, "READ", ""},
                                   {&reader::icons::kDot, "SELECT", ""},
                                   {&reader::icons::kUp, "UP", ""},
                                   {&reader::icons::kDown, "DOWN", ""}};
    int slotX[4] = {};
    const int barH = reader::drawHintBar(fb, f.fonts, hints, slotX);
    const int barTop = fb.height() - barH;
    for (int i = 0; i < 4; ++i) {
      CAPTURE(i);
      const int iconW = hints[i].icon->w;
      const int textX = slotX[i] + iconW + reader::kHintIconGap;
      const int slotEnd = i < 3 ? slotX[i + 1] : width - reader::kMargin;
      const Rows mark = inkRowsIn(fb, barTop + 1, fb.height(), slotX[i], slotX[i] + iconW);
      const Rows text = inkRowsIn(fb, barTop + 1, fb.height(), textX, slotEnd);
      REQUIRE(mark.bottom > 0);
      REQUIRE(text.bottom > 0);
      const int boxCentre2 = centre2(mark) - iconInkOffset2(*hints[i].icon);
      CHECK(boxCentre2 >= centre2(text) - 3);
      CHECK(boxCentre2 <= centre2(text) + 3);
    }
  }
}

// --- The row's type role and tracking ---------------------------------------

TEST_CASE("tracking is the board's letter-spacing, resolved from the type ramp") {
  // The boards state letter-spacing in em per run and the runs disagree. These
  // are those six values at the ramp's pixel sizes; if one drifts, the run it
  // belongs to is no longer the design's.
  CHECK(reader::kBandLabelTracking == 5);   // 0.22em at 23px = 5.06
  CHECK(reader::kRowLabelTracking == 4);    // 0.18em at 23px = 4.14
  CHECK(reader::kBlockLabelTracking == 5);  // 0.20em at 23px = 4.60
  CHECK(reader::kHintTracking == 3);        // 0.12em at 21px = 2.52
  CHECK(reader::kMetaTracking == 3);        // 0.16em at 21px = 3.36
  CHECK(reader::kTightMetaTracking == 2);   // 0.10em at 21px = 2.10
  // Not all one number, which is the defect this replaced: a single shared
  // constant cannot be right for six different runs.
  CHECK(reader::kRowLabelTracking != reader::kTightMetaTracking);
}

TEST_CASE("a row sets its label in Role::Label with the board's tracking") {
  Fixture f;
  const int width = 480;
  // Inked width of a reference run drawn the same way, so this compares like
  // with like rather than against Font::measure (which counts the trailing
  // advance and the last glyph's side bearing).
  auto refInkWidth = [&](reader::Role role, int tracking) {
    reader::Framebuffer ref(width, 200);
    reader::drawText(ref, f.fonts[role], reader::kMargin, 120, "LIBRARY", reader::Ink::Black,
                     tracking);
    int lo = width, hi = -1;
    for (int y = 0; y < 200; ++y)
      for (int x = 0; x < width; ++x)
        if (!ref.getPixel(x, y)) {
          if (x < lo) lo = x;
          if (x > hi) hi = x;
        }
    return hi - lo;
  };

  reader::Framebuffer fb(width, 200);
  reader::drawRow(fb, f.fonts, 0, "LIBRARY", "", /*focused=*/false);
  int lo = width, hi = -1;
  for (int y = 1; y < reader::kRowH; ++y)
    for (int x = 0; x < 240; ++x)
      if (!fb.getPixel(x, y)) {
        if (x < lo) lo = x;
        if (x > hi) hi = x;
      }
  const int rowW = hi - lo;

  // Exactly the Label face at the board's 0.18em...
  CHECK(rowW == refInkWidth(reader::Role::Label, reader::kRowLabelTracking));
  // ...and demonstrably neither of the two things it used to be: Body, and
  // untracked. Without these the equality above would still pass if every role
  // happened to measure alike.
  CHECK(rowW != refInkWidth(reader::Role::Label, 0));
  CHECK(rowW != refInkWidth(reader::Role::Body, 0));

  // And the label is set at Label's size, not Body's: the run is shorter than
  // the 14pt face's caps by the four pixels between a 23px and a 29px ramp step.
  const Rows ink = inkRowsIn(fb, 1, reader::kRowH, reader::kMargin, 240);
  CHECK(ink.bottom - ink.top < f.fonts[reader::Role::Body].ascent());
}

TEST_CASE("hint slots distribute across the canvas and never overlap") {
  Fixture f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", ""},
                                   {&reader::icons::kDot, "OPEN", "HOLD"},
                                   {&reader::icons::kUp, "UP", ""},
                                   {&reader::icons::kDown, "DOWN", ""}};
    int slotX[4] = {};
    reader::drawHintBar(fb, f.fonts, hints, slotX);
    for (int i = 1; i < 4; ++i) CHECK(slotX[i] > slotX[i - 1]);
    CHECK(slotX[0] >= reader::kMargin);
    CHECK(slotX[3] < width - reader::kMargin);
  }
}
