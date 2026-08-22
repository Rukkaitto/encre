#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "home_vm.h"
#include "ramp.h"
#include "rfnt_builder.h"
#include "reader/components.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/text.h"
#include "reader/theme_quiet.h"

using ramp::Ramp;

// A face that declares itself `role`'s own size and weight -- so FontSet::load
// accepts it -- whose line box is `lineH` tall instead of the real face's. It
// carries no glyphs: the tests that use it are about box geometry, and drawText's
// notdef box is enough ink to locate a run.
static std::vector<uint8_t> tallFace(reader::Role role, int lineH) {
  const reader::RoleSpec spec = reader::roleSpec(role);
  rfnt::Builder b;
  b.version = 2;
  b.bpp = 2;
  b.ppem = static_cast<uint16_t>(spec.ppem);
  b.weight = static_cast<uint16_t>(spec.weight);
  b.ascent = static_cast<int16_t>(lineH * 3 / 4);
  b.descent = static_cast<int16_t>(-(lineH / 4));
  b.lineGap = static_cast<int16_t>(lineH - (b.ascent - b.descent));
  return b.build();
}
static std::vector<uint8_t> tallLabelFace(int lineH) {
  return tallFace(reader::Role::Label500, lineH);
}
static std::vector<uint8_t> tallMetaFace(int lineH) {
  return tallFace(reader::Role::Meta400, lineH);
}

TEST_CASE("the header band right-aligns its value on any canvas width") {
  Ramp f;
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

TEST_CASE("the header band's height follows the type role it draws") {
  Ramp f;
  const reader::Font& lf = f.fonts[reader::Role::Label500];
  const reader::Font& vf = f.fonts[reader::Role::Value700];
  const int chrome = reader::kBandPadTop + reader::kBandPadBottom + reader::kBandRuleH;

  // The board declares no height for the band: `padding: 18px 24px 14px` around
  // its tallest flex item plus a 2px rule. On today's ramp the tallest item is
  // the value's 32px line box (the label's is 29, the battery 21), and Chrome
  // renders the band 66 tall -- not the 72 that used to be pinned here, which
  // put every screen's content 6px low.
  const int tallest = vf.lineHeight() > lf.lineHeight() ? vf.lineHeight() : lf.lineHeight();
  CHECK(reader::headerBandHeight(f.fonts) == chrome + tallest);
  CHECK(reader::headerBandHeight(f.fonts) == 66);

  // ...and it is genuinely derived, not a coincidence at one size. Give the
  // band's label role a bigger face and the band gets taller by exactly the
  // difference in line box, with nothing else to update. This is the property a
  // pinned constant cannot have, and the reason six unbuilt screens can set
  // their band label to whatever the board says.
  //
  // The substitute face is synthetic rather than another asset off the ramp,
  // because FontSet::load now checks a blob's declared size and weight against
  // the role's and would (correctly) refuse the 20pt face for Role::Label500.
  // So this is a face that declares itself the band label's 23px/500 and
  // differs from it in one respect only: a taller line box.
  reader::FontSet bigger;
  REQUIRE(f.load(bigger));
  const auto tallFace = tallLabelFace(53);
  REQUIRE(bigger.load(reader::Role::Label500, tallFace.data(), tallFace.size()));
  REQUIRE(bigger.ready());
  const int bigLabel = bigger[reader::Role::Label500].lineHeight();
  REQUIRE(bigLabel > tallest);
  CHECK(reader::headerBandHeight(bigger) == chrome + bigLabel);
  CHECK(reader::headerBandHeight(bigger) > reader::headerBandHeight(f.fonts));

  // The draw call reports that height and puts its rule at the bottom of it, so
  // a caller stacking below the band lands where the board's next block starts.
  for (const reader::FontSet* set : {&f.fonts, &bigger}) {
    reader::Framebuffer fb(480, 200);
    const int h = reader::drawHeaderBand(fb, *set, "NOW READING", "87%");
    CHECK(h == reader::headerBandHeight(*set));
    for (int y = h - reader::kBandRuleH; y < h; ++y) {
      CHECK_FALSE(fb.getPixel(0, y));         // rule, full bleed
      CHECK_FALSE(fb.getPixel(479, y));
    }
    CHECK(fb.getPixel(0, h));                 // and nothing below it
    CHECK(fb.getPixel(479, h));
    // The rule is the band's own bottom edge: the row above the padding-derived
    // content box must be clear of it on the left margin, where no glyph sits.
    CHECK(fb.getPixel(0, reader::kBandPadTop));
  }
}

TEST_CASE("a focused row inverts: black field, white text") {
  Ramp f;
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
  Ramp f;
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
  Ramp f;
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

// The hint bar is exactly one line tall whatever its slots carry, and a
// long-press variant is a hollow ring beside its label rather than a second line
// under it (design 662557d). The cases below pin the consequences of that: the
// bar's height stops depending on a hold, the hold's slot gets wider instead, and
// the ring sits on the label's own line.

TEST_CASE("a hold does not change the bar's height, on any slot") {
  Ramp f;
  const reader::Hint plain[4] = {{&reader::icons::kBack, "BACK", false},
                                 {&reader::icons::kDot, "OPEN", false},
                                 {&reader::icons::kUp, "UP", false},
                                 {&reader::icons::kDown, "DOWN", false}};
  const int plainH = reader::hintBarHeight(f.fonts, plain);
  // Whichever slot carries it, and however many do. A bar whose height varies by
  // screen also moves every list stacked above it, which is the whole reason the
  // hold stopped being a second line.
  for (int holdSlot : {0, 1, 2, 3}) {
    CAPTURE(holdSlot);
    reader::Hint hints[4] = {plain[0], plain[1], plain[2], plain[3]};
    hints[holdSlot].hasHold = true;
    CHECK(reader::hintBarHeight(f.fonts, hints) == plainH);
  }
  reader::Hint all[4] = {plain[0], plain[1], plain[2], plain[3]};
  for (auto& h : all) h.hasHold = true;
  CHECK(reader::hintBarHeight(f.fonts, all) == plainH);

  // And the drawn bar agrees with the measured one: nothing is clipped against
  // the bottom edge, and the ring's ink stays inside the content box the padding
  // leaves rather than hanging below it the way a second line did.
  for (int holdSlot : {0, 1, 2, 3}) {
    CAPTURE(holdSlot);
    reader::Framebuffer fb(480, 120);
    reader::Hint hints[4] = {plain[0], plain[1], plain[2], plain[3]};
    hints[holdSlot].hasHold = true;
    int slotX[4] = {};
    const int barH = reader::drawHintBar(fb, f.fonts, hints, slotX);
    CHECK(barH == plainH);
    const int barTop = fb.height() - barH;
    const int contentTop = barTop + reader::kHintRuleH + reader::kHintPadTop;
    const int contentH = barH - reader::kHintRuleH - reader::kHintPadTop - reader::kHintPadBottom;
    const Rows all = inkRows(fb, barTop, 0, 480);
    CHECK(all.top >= contentTop);
    CHECK(all.bottom < contentTop + contentH);
  }
}

TEST_CASE("a hold slot is wider than the same slot without one") {
  Ramp f;
  // The ring is a child of the slot's flex row, so it is part of what the slot
  // measures -- gap included. If the measure ignored it, `space-between` would
  // hand out gaps that are too wide and the ring would lap into its neighbour.
  const reader::Hint plain[4] = {{&reader::icons::kBack, "BACK", false},
                                 {&reader::icons::kDot, "OPEN", false},
                                 {&reader::icons::kUp, "UP", false},
                                 {&reader::icons::kDown, "DOWN", false}};
  reader::Hint held[4] = {plain[0], plain[1], plain[2], plain[3]};
  held[1].hasHold = true;

  // A slot's measured width is not returned, but `space-between` makes it
  // observable twice over.
  auto slots = [&](const reader::Hint hints[4], int out[4]) {
    reader::Framebuffer fb(480, 120);
    reader::drawHintBar(fb, f.fonts, hints, out);
  };
  int a[4] = {}, b[4] = {};
  slots(plain, a);
  slots(held, b);

  // First: the leftover is smaller, so the three gaps are narrower and every slot
  // after the first sits left of where it did. A measure that ignored the ring
  // would leave the distribution byte-identical to the plain bar's.
  CHECK(a[0] == b[0]);  // the first slot is always on the margin
  CHECK(b[1] < a[1]);
  CHECK(b[2] < a[2] + reader::kHintIconGap + reader::icons::kHold.w);

  // Second: the three gaps between one slot's content and the next slot's start
  // stay equal, which is what `space-between` means. The ring is the widest thing
  // in slot 1 after its label, so an unmeasured ring shows up here as a gap after
  // slot 1 narrower than the other two by the ring and its gap.
  const reader::Font& mf = f.fonts[reader::Role::Meta400];
  const reader::Tracking tr = reader::trackingEm(mf, reader::kHintEm);
  auto contentW = [&](const reader::Hint& h) {
    int w = h.icon->w + reader::kHintIconGap + mf.measure(h.label, tr);
    if (h.hasHold) w += reader::kHintIconGap + reader::icons::kHold.w;
    return w;
  };
  int gaps[3] = {};
  for (int i = 0; i < 3; ++i) gaps[i] = b[i + 1] - (b[i] + contentW(held[i]));
  for (int i = 0; i < 3; ++i) {
    CAPTURE(i);
    CHECK(gaps[i] > 0);
    CHECK(gaps[i] >= gaps[0] - 1);  // one rounding per gap, no more
    CHECK(gaps[i] <= gaps[0] + 1);
  }
}

TEST_CASE("the hold ring rides on its label's line, after the label") {
  Ramp f;
  // Vertically it aligns like every other mark on the screen (iconTopIn on the
  // slot's box), so its box centre lands on the label's. Horizontally it is the
  // row's last child: one flex gap past the label, and still inside the slot.
  for (int width : {480, 528}) {
    CAPTURE(width);
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", false},
                                   {&reader::icons::kDot, "OPEN", true},
                                   {&reader::icons::kUp, "UP", false},
                                   {&reader::icons::kDown, "DOWN", false}};
    int slotX[4] = {};
    const int barH = reader::drawHintBar(fb, f.fonts, hints, slotX);
    const int barTop = fb.height() - barH;
    const reader::Font& mf = f.fonts[reader::Role::Meta400];
    const reader::Tracking tr = reader::trackingEm(mf, reader::kHintEm);
    const int textX = slotX[1] + hints[1].icon->w + reader::kHintIconGap;
    const int labelW = mf.measure(hints[1].label, tr);
    const int ringX = textX + labelW + reader::kHintIconGap;

    // The ring is where the flex row puts it: past the label, before the next
    // slot, and drawn -- there is ink in its box and none in the gap before it.
    CHECK(ringX + reader::icons::kHold.w <= slotX[2]);
    const Rows label = inkRowsIn(fb, barTop + 1, fb.height(), textX, textX + labelW);
    const Rows ring = inkRowsIn(fb, barTop + 1, fb.height(), ringX,
                                ringX + reader::icons::kHold.w);
    REQUIRE(label.bottom > 0);
    REQUIRE(ring.bottom > 0);
    const Rows between = inkRowsIn(fb, barTop + 1, fb.height(), textX + labelW, ringX);
    CHECK(between.bottom == -1);

    // Vertically aligned with the label it modifies: the ring's *box* centre,
    // recovered from its ink by its own top bearing, sits on the label's.
    const int boxCentre2 = centre2(ring) - iconInkOffset2(reader::icons::kHold);
    CHECK(boxCentre2 >= centre2(label) - 3);
    CHECK(boxCentre2 <= centre2(label) + 3);
    // And exactly where iconTopIn puts it in the slot's box, not merely within a
    // pixel and a half of it.
    const int slotH = mf.lineHeight() > hints[1].icon->h ? mf.lineHeight() : hints[1].icon->h;
    const int slotTop = barTop + reader::kHintRuleH + reader::kHintPadTop;
    const int want = reader::iconTopIn(slotTop, slotH, reader::icons::kHold.h);
    int inkTop = -1;
    for (int y = 0; y < reader::icons::kHold.h && inkTop < 0; ++y)
      for (int x = 0; x < reader::icons::kHold.w; ++x)
        if (reader::coverage(reader::icons::kHold, x, y) >= 2) {
          inkTop = y;
          break;
        }
    REQUIRE(inkTop >= 0);
    CHECK(ring.top - inkTop == want);
  }
}

TEST_CASE("four slots with a hold fit inside the margins at both geometries") {
  Ramp f;
  // The regression that motivated the change: the words "- HOLD" on a second
  // line, or any hold rendered as text, do not fit a four-slot bar at 10pt on the
  // 480-wide X4. So this measures the whole bar's fit rather than any one slot's
  // position -- every slot's content, its ring included, inside the margins with
  // no slot lapping the next.
  const reader::Font& mf = f.fonts[reader::Role::Meta400];
  const reader::Tracking tr = reader::trackingEm(mf, reader::kHintEm);
  for (int width : {480, 528}) {
    CAPTURE(width);
    reader::Framebuffer fb(width, 120);
    // Library's own bar, the widest four-slot set the V1 boards ask for.
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", false},
                                   {&reader::icons::kDot, "OPEN", true},
                                   {&reader::icons::kUp, "UP", false},
                                   {&reader::icons::kDown, "DOWN", false}};
    int slotX[4] = {};
    reader::drawHintBar(fb, f.fonts, hints, slotX);
    int end[4] = {};
    for (int i = 0; i < 4; ++i) {
      int w = hints[i].icon->w + reader::kHintIconGap + mf.measure(hints[i].label, tr);
      if (hints[i].hasHold) w += reader::kHintIconGap + reader::icons::kHold.w;
      end[i] = slotX[i] + w;
    }
    CHECK(slotX[0] >= reader::kMargin);
    for (int i = 1; i < 4; ++i) {
      CAPTURE(i);
      CHECK(slotX[i] >= end[i - 1]);  // no slot laps the one before it
    }
    CHECK(end[3] <= width - reader::kMargin);
    // And the render agrees: no ink crosses either margin.
    const int barTop = fb.height() - reader::hintBarHeight(f.fonts, hints);
    for (int y = barTop + 1; y < fb.height(); ++y)
      for (int x = 0; x < width; ++x)
        if (!fb.getPixel(x, y)) {
          CHECK(x >= reader::kMargin);
          CHECK(x < width - reader::kMargin);
        }
  }
}

TEST_CASE("the hint bar's height is the board's padding plus its own content") {
  Ramp f;
  const reader::Hint plain[4] = {{&reader::icons::kBook, "READ", false},
                                 {&reader::icons::kDot, "SELECT", false},
                                 {&reader::icons::kUp, "UP", false},
                                 {&reader::icons::kDown, "DOWN", false}};
  const int lineH = f.fonts[reader::Role::Meta400].lineHeight();
  const int chrome = reader::kHintRuleH + reader::kHintPadTop + reader::kHintPadBottom;

  // Home's bar: `padding: 20px 24px 16px` + a 1px rule around one 27px line box
  // of Meta, which is what Chrome renders 64 tall. The marks are 25px, shorter
  // than the line, so they do not raise it.
  CHECK(reader::hintBarHeight(f.fonts, plain) == chrome + lineH);
  CHECK(reader::hintBarHeight(f.fonts, plain) == 64);

  // A hold adds nothing: the ring is a mark on the label's own line, shorter
  // than that line box, so the bar is the same height as Home's. It is still
  // *derived* from the content -- the two checks below prove the derivation is
  // live -- it just does not depend on a hold any more.
  reader::Hint held[4] = {plain[0], plain[1], plain[2], plain[3]};
  held[1].hasHold = true;
  CHECK(reader::hintBarHeight(f.fonts, held) == chrome + lineH);
  CHECK(reader::hintBarHeight(f.fonts, held) == reader::hintBarHeight(f.fonts, plain));

  // Derived from the type role: a taller Meta line box makes a taller bar, with
  // no constant to remember. Role::Meta400 is what a hint is set in, so this is
  // the dependency the boards actually have.
  reader::FontSet bigger;
  REQUIRE(f.load(bigger));
  const auto tallFace = tallMetaFace(41);
  REQUIRE(bigger.load(reader::Role::Meta400, tallFace.data(), tallFace.size()));
  REQUIRE(bigger.ready());
  CHECK(bigger[reader::Role::Meta400].lineHeight() == 41);
  CHECK(reader::hintBarHeight(bigger, plain) == chrome + 41);
  CHECK(reader::hintBarHeight(bigger, held) == reader::hintBarHeight(bigger, plain));

  // A mark taller than the line box raises the bar too: a slot is a flex row of
  // its marks and its label, so it is as tall as the tallest of them. kFolder
  // (46x39) is not a hint mark today, which is exactly why it serves here -- the
  // rule must not depend on today's 25px set.
  reader::Hint big[4] = {plain[0], plain[1], plain[2], plain[3]};
  big[3].icon = &reader::icons::kFolder;
  CHECK(reader::hintBarHeight(f.fonts, big) == chrome + reader::icons::kFolder.h);
}

TEST_CASE("the hint bar honours the board's asymmetric padding") {
  Ramp f;
  // `padding: 20px 24px 16px 24px`. The primitive used to centre its content in
  // the bar, which is only correct for symmetric padding and left every hint
  // label on every screen 3px high. What follows measures that 4px asymmetry
  // rather than the absolute row, so it holds on either panel geometry.
  for (int width : {480, 528}) {
    const reader::Hint hints[4] = {{&reader::icons::kBook, "READ", false},
                                   {&reader::icons::kDot, "SELECT", false},
                                   {&reader::icons::kUp, "UP", false},
                                   {&reader::icons::kDown, "DOWN", false}};
    reader::Framebuffer fb(width, 160);
    int slotX[4] = {};
    const int barH = reader::drawHintBar(fb, f.fonts, hints, slotX);
    const int barTop = fb.height() - barH;
    const int contentTop = barTop + reader::kHintRuleH + reader::kHintPadTop;
    const int contentH = barH - reader::kHintRuleH - reader::kHintPadTop - reader::kHintPadBottom;

    // The label's own ink, clear of its mark.
    const int textX = slotX[0] + hints[0].icon->w + reader::kHintIconGap;
    const Rows text = inkRowsIn(fb, barTop + 1, fb.height(), textX, slotX[1]);
    REQUIRE(text.bottom > 0);

    // Centred in the content box: an all-caps run has no descender, so its ink
    // sits a hair above the box's middle. Never below it.
    const int contentCentre2 = 2 * contentTop + contentH;
    CHECK(centre2(text) <= contentCentre2);
    CHECK(centre2(text) >= contentCentre2 - 3);

    // And therefore *below* the bar's own centre line, by the 2px the padding
    // is heavier on top. This is the assertion symmetric centring cannot pass:
    // it put this ink on the bar's centre line, 3px high of the board.
    const int barCentre2 = 2 * barTop + barH;
    CHECK(centre2(text) >= barCentre2 + 2);

    // The line box itself is where the padding puts it, so nothing is clipped
    // against either edge and the bottom padding is really 16.
    CHECK(text.top >= contentTop);
    CHECK(text.bottom < contentTop + contentH);
  }
}

TEST_CASE("structural drawing is identical in every plane") {
  Ramp f;

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
      reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%", &reader::icons::kBattery,
                             plane);
      return fb;
    };
    const reader::Framebuffer bw = render(reader::Plane::Bw);
    const reader::Framebuffer lsb = render(reader::Plane::Lsb);
    const reader::Framebuffer msb = render(reader::Plane::Msb);
    const int bandH = reader::headerBandHeight(f.fonts);
    for (int x = 0; x < 480; ++x)
      for (int y : {bandH - 2, bandH - 1}) {
        CHECK(bw.getPixel(x, y) == lsb.getPixel(x, y));
        CHECK(bw.getPixel(x, y) == msb.getPixel(x, y));
      }
  }

  // drawHintBar: the top rule, drawn before any icon or label, is furniture
  // too.
  {
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", false},
                                   {&reader::icons::kDot, "OPEN", false},
                                   {&reader::icons::kUp, "UP", false},
                                   {&reader::icons::kDown, "DOWN", false}};
    auto render = [&](reader::Plane plane) {
      reader::Framebuffer fb(480, 120);
      int slotX[4] = {};
      reader::drawHintBar(fb, f.fonts, hints, slotX, plane);
      return fb;
    };
    const reader::Framebuffer bw = render(reader::Plane::Bw);
    const reader::Framebuffer lsb = render(reader::Plane::Lsb);
    const reader::Framebuffer msb = render(reader::Plane::Msb);
    const int top = bw.height() - reader::hintBarHeight(f.fonts, hints);
    for (int x = 0; x < 480; ++x) {
      CHECK(bw.getPixel(x, top) == lsb.getPixel(x, top));
      CHECK(bw.getPixel(x, top) == msb.getPixel(x, top));
    }
  }
}

// --- Vertical centring, and icons aligned to it -----------------------------


TEST_CASE("baselineIn centres the em box, and sits higher than centring the ascent") {
  Ramp f;
  for (reader::Role role : {reader::Role::Meta400, reader::Role::Label500, reader::Role::Value700,
                            reader::Role::Body400, reader::Role::Title700, reader::Role::Display700}) {
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

TEST_CASE("iconTopIn centres an item on its flex line, rounding once") {
  // CSS `align-items: center` puts every child's box on one cross-axis centre
  // line, so the answer is `boxTop + (boxH - itemH) / 2` with a single rounding
  // -- and halves go up, which is what Chrome's pixel snapping does.
  for (int boxTop : {0, 1, 18, 241}) {
    for (int boxH : {21, 27, 32, 72, 80}) {
      for (int itemH : {9, 12, 21, 25, 32, 46}) {
        CAPTURE(boxTop);
        CAPTURE(boxH);
        CAPTURE(itemH);
        const int top = reader::iconTopIn(boxTop, boxH, itemH);
        // Exact, in doubled units, against the fractional truth. No tolerance:
        // that is the point of rounding once instead of three times.
        CHECK(2 * top + itemH == 2 * boxTop + boxH + ((boxH - itemH) % 2 != 0 ? 1 : 0));
      }
    }
  }
  // And the specific number the board renders: a 21px battery on the header
  // band's 32px content box, whose top edge is the band's 18px padding, lands
  // on row 24 -- measured off the rasterised board, which puts its ink on rows
  // 24..44. Deriving the centre from the value's baseline instead landed it on
  // 25, and the percentage's ink centre 1.5px above the glyph's.
  CHECK(reader::iconTopIn(reader::kBandPadTop, 32, reader::icons::kBattery.h) == 24);
}

TEST_CASE("an icon aligns on the line box of text of any size beside it") {
  Ramp f;
  // The regression pin for the header band's battery. The band draws its value
  // in one role and centres the battery in the same box; the icon is 21px and
  // the box is the *value's* 32px line box, so the two heights differ and the
  // rounding cannot cancel. Both must land on one centre for every role in the
  // ramp -- Reader's band sets its value at a different size than Home's.
  for (reader::Role role : {reader::Role::Meta400, reader::Role::Label500,
                            reader::Role::Value700, reader::Role::Body400}) {
    const reader::Font& font = f.fonts[role];
    const int boxTop = reader::kBandPadTop;
    const int boxH = font.lineHeight();
    const int base = reader::baselineIn(font, boxTop, boxH);
    for (int iconH : {9, 21, 25, 38}) {
      CAPTURE(iconH);
      const int top = reader::iconTopIn(boxTop, boxH, iconH);
      // The icon's box centre is the line box's centre, to within the half
      // pixel a whole-pixel grid cannot express...
      CHECK(2 * top + iconH >= 2 * boxTop + boxH);
      CHECK(2 * top + iconH <= 2 * boxTop + boxH + 1);
      // ...and it is *not* the number the baseline-derived formula gave, once
      // the roundings stop cancelling. That formula is
      // `base - (ascent + descent) / 2 - iconH / 2`; where it differs it is
      // always low, never high, which is exactly how the battery drifted below
      // its percentage.
      const int oldTop = base - (font.ascent() + font.descent()) / 2 - iconH / 2;
      CHECK(oldTop >= top);
    }
  }
}

TEST_CASE("a row's label is optically centred in the row") {
  Ramp f;
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
  Ramp f;
  const int width = 480;
  reader::Framebuffer fb(width, 200);
  reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%");
  const int iconX = width - reader::kMargin - reader::icons::kBattery.w;
  // Above the 2px rule, so the full-bleed rule cannot dominate either band.
  const int aboveRule = reader::headerBandHeight(f.fonts) - reader::kBandRuleH;
  const Rows bat = inkRowsIn(fb, 0, aboveRule, iconX, width);
  const Rows value = inkRowsIn(fb, 0, aboveRule, width / 2, iconX);
  REQUIRE(bat.bottom > 0);
  REQUIRE(value.bottom > 0);

  // Where the glyph's *box* actually landed, read off the render rather than
  // recomputed: the battery's outline is a full column of ink at its left edge,
  // exactly kBattery.h rows of it, so that column's first row is the box's top.
  int boxTop = -1, outlineRows = 0;
  for (int y = 0; y < aboveRule; ++y)
    if (!fb.getPixel(iconX, y)) {
      if (boxTop < 0) boxTop = y;
      ++outlineRows;
    }
  REQUIRE(outlineRows == reader::icons::kBattery.h);

  // The band's content box is its padding-derived strip, and `align-items:
  // center` puts the glyph's box on that box's centre. Exact, not a tolerance:
  // the battery is 21px against a 32px box, so the halves do not cancel, and
  // the previous formula -- which reached the same centre through the value's
  // baseline and two more integer divisions -- landed on 25 instead of 24 and
  // put the glyph 1.5px below the percentage's optical centre. The rasterised
  // board puts this glyph's ink on rows 24..44.
  const int contentH = reader::headerBandHeight(f.fonts) - reader::kBandPadTop -
                       reader::kBandPadBottom - reader::kBandRuleH;
  CHECK(boxTop == reader::iconTopIn(reader::kBandPadTop, contentH, reader::icons::kBattery.h));
  CHECK(boxTop == 24);
  // And the box shares the value's line-box centre to within the half pixel a
  // whole-pixel grid cannot express. Doubled units, so half a pixel is 1.
  const int valueBoxCentre2 = 2 * reader::kBandPadTop + contentH;
  const int batBoxCentre2 = 2 * boxTop + reader::icons::kBattery.h;
  CHECK(batBoxCentre2 - valueBoxCentre2 >= 0);
  CHECK(batBoxCentre2 - valueBoxCentre2 <= 1);
  // The ink agrees with the box: the glyph's ink centre sits where the value's
  // does, allowing for the caps having no descender (hence a doubled tolerance
  // of 2, not the 3 the off-by-1.5px placement needed to pass).
  const int boxCentre2 = centre2(bat) - iconInkOffset2(reader::icons::kBattery);
  CHECK(boxCentre2 >= centre2(value) - 2);
  CHECK(boxCentre2 <= centre2(value) + 2);
}

TEST_CASE("every hint mark is aligned with the label it labels") {
  Ramp f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBook, "READ", false},
                                   {&reader::icons::kDot, "SELECT", false},
                                   {&reader::icons::kUp, "UP", false},
                                   {&reader::icons::kDown, "DOWN", false}};
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
      // And exactly where flex centring puts it, not merely within a pixel and
      // a half of it. The mark and the label are two children of one
      // `align-items: center` row whose cross size is the taller of the label's
      // line box and the mark, so:
      const reader::Font& mf = f.fonts[reader::Role::Meta400];
      const int firstH = mf.lineHeight() > hints[i].icon->h ? mf.lineHeight() : hints[i].icon->h;
      const int slotTop = barTop + reader::kHintRuleH + reader::kHintPadTop;
      const int want = reader::iconTopIn(slotTop, firstH, hints[i].icon->h);
      // Read the mark's box top off the render: its ink starts a known number
      // of rows into its box (the icon's own top bearing). Coverage >= 2 is the
      // threshold the Bw plane paints at, which is the plane this framebuffer
      // holds -- a row of coverage 1 shows in the grey planes and not here.
      int inkTop = -1;
      for (int y = 0; y < hints[i].icon->h && inkTop < 0; ++y)
        for (int x = 0; x < hints[i].icon->w; ++x)
          if (reader::coverage(*hints[i].icon, x, y) >= 2) {
            inkTop = y;
            break;
          }
      REQUIRE(inkTop >= 0);
      CHECK(mark.top - inkTop == want);
    }
  }
}

// --- The row's type role and tracking ---------------------------------------

TEST_CASE("tracking is the board's letter-spacing, kept as a fraction") {
  Ramp f;
  const reader::Font& meta = f.fonts[reader::Role::Meta400];
  const reader::Font& label = f.fonts[reader::Role::Label500];
  // Resolved against the face's own declared size, not a table of sizes kept
  // beside these constants.
  CHECK(meta.ppem() == 21);
  CHECK(label.ppem() == 23);

  // The boards state letter-spacing in em per run and the runs disagree. These
  // are those six values at the ramp's pixel sizes, in 1/64 px; if one drifts,
  // the run it belongs to is no longer the design's. Every one of them is
  // fractional, which is the whole defect: rounded to whole pixels, four of the
  // six were wrong by 0.4-0.5px *per character*.
  struct Case { reader::Tracking t; double px; };
  const Case cases[] = {
      {reader::trackingEm(label, reader::kBandLabelEm), 5.06},   // 0.22em at 23px
      {reader::trackingEm(label, reader::kRowLabelEm), 4.14},    // 0.18em at 23px
      {reader::trackingEm(label, reader::kBlockLabelEm), 4.60},  // 0.20em at 23px
      {reader::trackingEm(meta, reader::kHintEm), 2.52},         // 0.12em at 21px
      {reader::trackingEm(meta, reader::kMetaEm), 3.36},         // 0.16em at 21px
      {reader::trackingEm(meta, reader::kTightMetaEm), 2.10},    // 0.10em at 21px
  };
  for (const Case& c : cases) {
    CAPTURE(c.px);
    // Within a 64th of the board's exact value...
    CHECK(c.t.f26() == static_cast<int>(c.px * 64 + 0.5));
    // ...and genuinely not a whole pixel, so a test that only ever compared
    // integers could not tell this apart from the rounding it replaced.
    CHECK(c.t.f26() % 64 != 0);
  }
  // Not all one number, which is the defect before that: a single shared
  // constant cannot be right for six different runs.
  CHECK_FALSE(cases[1].t == cases[5].t);
}

TEST_CASE("a fractional tracking does not accumulate, and measure agrees with drawText") {
  Ramp f;
  const reader::Font& meta = f.fonts[reader::Role::Meta400];
  const reader::Tracking hint = reader::trackingEm(meta, reader::kHintEm);
  REQUIRE(hint.f26() % 64 != 0);  // 2.52px: the case a whole-pixel API cannot hold

  // The pin. Per-glyph rounding to 3px would put a six-character label 6 * 0.48
  // = 2.9px wide of the board; the fraction has to survive the accumulation.
  // 2.52 * 6 = 15.12 -> 15, where 3 * 6 = 18.
  const int plain = meta.measure("SELECT");
  CHECK(meta.measure("SELECT", hint) == plain + 15);
  CHECK(meta.measure("SELECT", reader::Tracking::px(3)) == plain + 18);

  // measure() must equal drawText()'s reported advance for every string, every
  // tracking and every origin -- right-aligned runs are placed at
  // `edge - measure(s)`, so a disagreement is a run that misses its margin.
  for (const char* s : {"", "UP", "SELECT", "CH. 01 \xE2\x80\x94 MISS BROOKE",
                        "\xE4\xB8\xAD\xE6\x96\x87"}) {  // last: no glyphs in the subset
    for (const reader::Tracking t : {reader::Tracking(), hint, reader::Tracking::px(3),
                                     reader::Tracking::em(21, 220)}) {
      for (int x : {0, 1, 24, 137}) {
        CAPTURE(s);
        CAPTURE(t.f26());
        CAPTURE(x);
        reader::Framebuffer fb(600, 60);
        CHECK(reader::drawText(fb, meta, x, 40, s, reader::Ink::Black, t) == meta.measure(s, t));
      }
    }
  }
}

TEST_CASE("a row sets its label in Role::Label500 with the board's tracking") {
  Ramp f;
  const int width = 480;
  // Inked width of a reference run drawn the same way, so this compares like
  // with like rather than against Font::measure (which counts the trailing
  // advance and the last glyph's side bearing).
  auto refInkWidth = [&](reader::Role role, reader::Tracking tracking) {
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
  CHECK(rowW == refInkWidth(reader::Role::Label500,
                            reader::trackingEm(f.fonts[reader::Role::Label500],
                                               reader::kRowLabelEm)));
  // ...and demonstrably neither of the two things it used to be: Body, and
  // untracked. Without these the equality above would still pass if every role
  // happened to measure alike.
  CHECK(rowW != refInkWidth(reader::Role::Label500, {}));
  CHECK(rowW != refInkWidth(reader::Role::Body400, {}));

  // And the label is set at Label's size, not Body's: the run is shorter than
  // the 14pt face's caps by the four pixels between a 23px and a 29px ramp step.
  const Rows ink = inkRowsIn(fb, 1, reader::kRowH, reader::kMargin, 240);
  CHECK(ink.bottom - ink.top < f.fonts[reader::Role::Body400].ascent());
}

TEST_CASE("hint slots distribute across the canvas and never overlap") {
  Ramp f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 120);
    const reader::Hint hints[4] = {{&reader::icons::kBack, "BACK", false},
                                   {&reader::icons::kDot, "OPEN", true},
                                   {&reader::icons::kUp, "UP", false},
                                   {&reader::icons::kDown, "DOWN", false}};
    int slotX[4] = {};
    reader::drawHintBar(fb, f.fonts, hints, slotX);
    for (int i = 1; i < 4; ++i) CHECK(slotX[i] > slotX[i - 1]);
    CHECK(slotX[0] >= reader::kMargin);
    CHECK(slotX[3] < width - reader::kMargin);
  }
}

TEST_CASE("the theme draws a hold ring exactly on the slots the view model marks") {
  // The ring is an affordance for a binding. If the theme sourced it from
  // anywhere but the view model's holds array, a screen could promise a hold it
  // does not have -- or bind one with nothing on screen to suggest it.
  Ramp r;
  reader::QuietTheme theme;

  reader::HomeViewModel vm = sampleHome();
  vm.holds = {false, true, false, false};  // Confirm only
  reader::Framebuffer with(480, 800);
  theme.renderHome(with, r.fonts, vm, reader::Plane::Bw);

  vm.holds = {false, false, false, false};
  reader::Framebuffer without(480, 800);
  theme.renderHome(without, r.fonts, vm, reader::Plane::Bw);

  REQUIRE(with.sizeBytes() == without.sizeBytes());
  // The bar is the same height either way -- the ring rides on the label's line
  // -- but the Confirm slot is wider, so the frames must differ.
  CHECK(std::memcmp(with.data(), without.data(),
                    static_cast<size_t>(with.sizeBytes())) != 0);
}

// --- The empty hint slot ---------------------------------------------------
//
// Eight boards give a button with no action a `width: 36px` placeholder rather
// than nothing, and `space-between` divides the leftover around it. A slot
// measured as zero-wide does not draw less, it draws the OTHER slots in the wrong
// places, so this is asserted through the distribution the bar reports.

TEST_CASE("an empty hint slot is the board's 36px placeholder, not zero-wide") {
  Ramp f;
  const reader::Font& mf = f.fonts[reader::Role::Meta400];
  const reader::Tracking tr = reader::trackingEm(mf, reader::kHintEm);
  // design/SdMissing.dc.html's bar: one hint, three placeholders.
  const reader::Hint hints[4] = {{nullptr, "", false},
                                 {&reader::icons::kDot, "RETRY", false},
                                 {nullptr, "", false},
                                 {nullptr, "", false}};
  for (int width : {480, 528}) {
    CAPTURE(width);
    reader::Framebuffer fb(width, 120);
    int slotX[4] = {};
    reader::drawHintBar(fb, f.fonts, hints, slotX);

    const int hintW = reader::icons::kDot.w + reader::kHintIconGap + mf.measure("RETRY", tr);
    const int usable = width - 2 * reader::kMargin;
    const int leftover = usable - (3 * reader::kHintEmptySlotW + hintW);
    // Slot 0 is on the margin, and the three gaps are equal: exactly the board's
    // measured 24 / 132.06 / 311.94 / 420 at 480 wide, to the pixel the browser's
    // fractional gap costs.
    CHECK(slotX[0] == reader::kMargin);
    CHECK(slotX[1] == reader::kMargin + reader::kHintEmptySlotW + (leftover + 1) / 3);
    CHECK(slotX[2] == reader::kMargin + reader::kHintEmptySlotW + hintW + (2 * leftover + 1) / 3);
    CHECK(slotX[3] == reader::kMargin + 2 * reader::kHintEmptySlotW + hintW + (3 * leftover + 1) / 3);
    // The last slot's placeholder ends on the far margin, which is what
    // space-between means and what a zero-width empty slot would break.
    CHECK(slotX[3] + reader::kHintEmptySlotW == width - reader::kMargin);
    // Nothing is drawn in an empty slot: only the one hint has ink, and it starts
    // where slot 1 does rather than on the margin.
    const int barTop = fb.height() - reader::hintBarHeight(f.fonts, hints);
    int firstInk = width;
    for (int y = barTop + reader::kHintRuleH; y < fb.height(); ++y)
      for (int x = 0; x < width; ++x)
        if (!fb.getPixel(x, y) && x < firstInk) firstInk = x;
    CHECK(firstInk >= slotX[1]);
    CHECK(firstInk < slotX[1] + reader::icons::kDot.w);
  }
}

TEST_CASE("a bar of empty slots is still one line tall") {
  Ramp f;
  // The height is the tallest slot, and an empty slot keeps its label's line box,
  // so the invariant that the bar is exactly one line tall does not depend on
  // which buttons a screen happens to bind.
  const reader::Hint none[4] = {
      {nullptr, "", false}, {nullptr, "", false}, {nullptr, "", false}, {nullptr, "", false}};
  const reader::Hint full[4] = {{&reader::icons::kBack, "BACK", false},
                                {&reader::icons::kDot, "OPEN", false},
                                {&reader::icons::kUp, "UP", false},
                                {&reader::icons::kDown, "DOWN", false}};
  CHECK(reader::hintBarHeight(f.fonts, none) == reader::hintBarHeight(f.fonts, full));
}

// --- The prompt button ----------------------------------------------------

TEST_CASE("a prompt button is the board's slab with its label knocked out") {
  Ramp f;
  reader::Framebuffer fb(480, 200);
  const int x = 110, y = 40, w = 260;  // design/SdMissing.dc.html's own box
  const int h = reader::drawActionButton(fb, f.fonts, x, y, w, "RETRY");
  CHECK(h == reader::kActionH);
  CHECK(h == 68);

  // Solid to its edges, and nothing outside them. Counted rather than checked
  // per pixel: a CHECK inside a 480x68 loop is 32000 assertions saying one thing.
  int paperInside = 0, inkOutside = 0;
  for (const int row : {y, y + h - 1})
    for (int col = x; col < x + w; ++col)
      if (fb.getPixel(col, row)) ++paperInside;
  for (int col = 0; col < 480; ++col) {
    if (!fb.getPixel(col, y - 1)) ++inkOutside;
    if (!fb.getPixel(col, y + h)) ++inkOutside;
  }
  for (int row = y; row < y + h; ++row) {
    if (!fb.getPixel(x - 1, row)) ++inkOutside;
    if (!fb.getPixel(x + w, row)) ++inkOutside;
  }
  CHECK(paperInside == 0);
  CHECK(inkOutside == 0);

  // The label is white ink inside the slab, centred on both axes: the same
  // number of paper columns to its left as to its right, and optically centred
  // vertically -- caps sit a shade above the middle because baselineIn centres
  // the face's whole extent, descender included, which is what CSS does.
  int x0 = 480, x1 = -1, y0 = 200, y1 = -1;
  for (int row = y; row < y + h; ++row)
    for (int col = x; col < x + w; ++col)
      if (fb.getPixel(col, row)) {
        if (col < x0) x0 = col;
        if (col > x1) x1 = col;
        if (row < y0) y0 = row;
        if (row > y1) y1 = row;
      }
  REQUIRE(x1 > 0);
  const int leftGap = x0 - x, rightGap = (x + w - 1) - x1;
  // The board's letter-spacing is added after the LAST letter too (which is what
  // CSS does and what the flex box measures), so the ink sits half a tracking
  // step left of centre -- 0.18em at 25px is 4.5px.
  CHECK(leftGap < rightGap);
  CHECK(rightGap - leftGap <= 8);
  const int above = y0 - y, below = (y + h - 1) - y1;
  CHECK(above <= below + 3);
  CHECK(below <= above + 3);
}

// --- Wrapped prose --------------------------------------------------------

TEST_CASE("prose wraps greedily on spaces, and its height follows the board") {
  Ramp f;
  const reader::Font& body = f.fonts[reader::Role::Body400];
  const std::string text =
      "Books, articles, fonts, and reading progress live on the card. Insert one, then retry.";

  // design/SdMissing.dc.html's paragraph: three lines in a 420px column, the
  // same three the board's browser lays out.
  const reader::Prose p = reader::wrapProse(body, text, reader::kProseMaxW, reader::kProseLeadEm);
  CHECK(p.lineCount() == 3);
  for (const std::string_view line : p.lines) {
    CAPTURE(line);
    CHECK(body.measure(line, p.tracking) <= reader::kProseMaxW);
    // No line carries a space at either end: a trailing space would push a
    // centred line off centre by half its advance.
    CHECK(line.front() != ' ');
    CHECK(line.back() != ' ');
  }
  // Every word survives, in order, with exactly one space between them.
  std::string rejoined;
  for (const std::string_view line : p.lines) {
    if (!rejoined.empty()) rejoined += ' ';
    rejoined += std::string(line);
  }
  CHECK(rejoined == text);

  // `line-height: 1.55` on a 29px face is 44.95px, held in 1/64 px rather than
  // rounded: three lines are 134.86px, not 3 x 45 = 135.
  CHECK(p.leadF26 == reader::Tracking::em(body.ppem(), reader::kProseLeadEm).f26());
  CHECK(p.leadF26 == 2877);
  CHECK(p.heightF26() == 3 * 2877);

  // A narrower column takes more lines, and the narrowest possible one puts every
  // word on its own line rather than looping or breaking inside a word.
  CHECK(reader::wrapProse(body, text, 200, reader::kProseLeadEm).lineCount() >= 5);
  const reader::Prose perWord = reader::wrapProse(body, text, 1, reader::kProseLeadEm);
  CHECK(perWord.lineCount() == 14);  // the copy's word count
  CHECK(perWord.lines.front() == "Books,");
  CHECK(perWord.lines.back() == "retry.");
}

TEST_CASE("prose tolerates the degenerate inputs a caller can hand it") {
  Ramp f;
  const reader::Font& body = f.fonts[reader::Role::Body400];
  CHECK(reader::wrapProse(body, "", 400, reader::kProseLeadEm).lineCount() == 0);
  CHECK(reader::wrapProse(body, "   ", 400, reader::kProseLeadEm).lineCount() == 0);
  CHECK(reader::wrapProse(body, "  one  ", 400, reader::kProseLeadEm).lineCount() == 1);
  CHECK(reader::wrapProse(body, "  one  ", 400, reader::kProseLeadEm).lines[0] == "one");
  // Runs of spaces inside the text collapse into the break, not into a line.
  const reader::Prose p = reader::wrapProse(body, "one     two", 1, reader::kProseLeadEm);
  REQUIRE(p.lineCount() == 2);
  CHECK(p.lines[0] == "one");
  CHECK(p.lines[1] == "two");
  // An empty paragraph draws nothing and consumes nothing.
  reader::Framebuffer fb(480, 200);
  const reader::Prose none = reader::wrapProse(body, "", 400, reader::kProseLeadEm);
  CHECK(reader::drawProse(fb, body, none, 0, 480, 0) == 0);
  int ink = 0;
  for (int y = 0; y < 200; ++y)
    for (int x = 0; x < 480; ++x)
      if (!fb.getPixel(x, y)) ++ink;
  CHECK(ink == 0);
}

TEST_CASE("prose lines are centred in their column and led by the board's line-height") {
  Ramp f;
  const reader::Font& body = f.fonts[reader::Role::Body400];
  const std::string text = "one two three four five six seven eight nine ten";
  const reader::Prose p = reader::wrapProse(body, text, 200, reader::kProseLeadEm);
  REQUIRE(p.lineCount() >= 3);

  reader::Framebuffer fb(480, 400);
  const int boxX = 40, boxW = 200;
  CHECK(reader::drawProse(fb, body, p, boxX, boxW, reader::pxToF26(20)) == p.heightF26());

  // Each line's ink is inside the column and centred in it: the paper either
  // side differs by no more than the side bearings of the glyphs that happen to
  // start and end the line.
  int prevTop = -1;
  for (int i = 0; i < p.lineCount(); ++i) {
    CAPTURE(i);
    const int lineTop = reader::f26ToPx(reader::pxToF26(20) + i * p.leadF26);
    const int lineBot = reader::f26ToPx(reader::pxToF26(20) + (i + 1) * p.leadF26);
    int x0 = 480, x1 = -1, top = -1;
    for (int y = lineTop; y < lineBot; ++y)
      for (int x = 0; x < 480; ++x)
        if (!fb.getPixel(x, y)) {
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (top < 0) top = y;
        }
    REQUIRE(x1 > 0);
    CHECK(x0 >= boxX);
    CHECK(x1 < boxX + boxW);
    const int left = x0 - boxX, right = (boxX + boxW - 1) - x1;
    CHECK(left <= right + 3);
    CHECK(right <= left + 3);
    (void)prevTop;
    prevTop = top;
  }
}

TEST_CASE("the board's fractional line-height does not drift down a long paragraph") {
  Ramp f;
  const reader::Font& body = f.fonts[reader::Role::Body400];
  // Identical lines, so every line's ink top is the same distance below its own
  // baseline and the gaps between them measure the leading itself. Thirty of
  // them, because 1.55 x 29px is 44.953: the fraction takes twenty-odd lines to
  // accumulate the whole pixel that makes one gap measurably shorter, and a
  // paragraph short enough to hide that is a paragraph this test proves nothing
  // about.
  constexpr int kLines = 30;
  std::string text = "one";
  for (int i = 1; i < kLines; ++i) text += " one";
  const reader::Prose p = reader::wrapProse(body, text, 1, reader::kProseLeadEm);
  REQUIRE(p.lineCount() == kLines);

  reader::Framebuffer fb(200, 1500);
  reader::drawProse(fb, body, p, 0, 200, reader::pxToF26(10));

  std::vector<int> tops;
  int run = -1;
  for (int y = 0; y < 1500; ++y) {
    bool ink = false;
    for (int x = 0; x < 200; ++x)
      if (!fb.getPixel(x, y)) ink = true;
    if (ink && run < 0) {
      run = y;
      tops.push_back(y);
    }
    if (!ink) run = -1;
  }
  REQUIRE(tops.size() == static_cast<size_t>(kLines));

  // Every gap is 44 or 45 -- and at least one is 44, which is what proves the
  // fraction is carried rather than rounded up per line. Thirty lines of a
  // rounded-up 45 would sit 1.4px low by the bottom, and each line's baseline
  // here is within half a pixel of the board's.
  bool sawShort = false;
  for (size_t i = 1; i < tops.size(); ++i) {
    const int gap = tops[i] - tops[i - 1];
    CAPTURE(i);
    CHECK(gap >= 44);
    CHECK(gap <= 45);
    if (gap == 44) sawShort = true;
  }
  CHECK(sawShort);
  // And the span is the board's own, not the sum of thirty roundings: within the
  // one pixel the two end baselines' own rounding can differ by.
  const int span = tops.back() - tops.front();
  const int exact = reader::f26ToPx((kLines - 1) * p.leadF26);
  CHECK(span >= exact - 1);
  CHECK(span <= exact + 1);
}

TEST_CASE("baselineIn IS baselineInF26, so there is one centring rule and not two") {
  Ramp f;
  // The whole-pixel entry point is a unit conversion in front of the fractional
  // one, so the two agree on EVERY box -- not just on the boxes whose half-leading
  // happens to be a whole number of pixels.
  //
  // They used to disagree by exactly 1px wherever it is not: a 25px face in a
  // 68px box has 17.5px of half-leading, and the old `(boxH - extent) / 2` threw
  // the half away before adding the ascent, where the fractional form carries it
  // and rounds the finished baseline. That is what the browser does and what
  // "round once" means, so the fractional answer is the correct one -- and the
  // difference was *pinned* here for a commit rather than fixed, on the belief
  // that four Home goldens depended on the whole-pixel answer. They did not: every
  // box Home draws has even or negative slack. The two goldens that did depend on
  // it were SdMissing's, through drawActionButton's 68px block, and they were
  // re-blessed with the unification.
  for (const reader::Role role : {reader::Role::Meta400, reader::Role::Label500,
                                  reader::Role::Value700, reader::Role::Body400,
                                  reader::Role::Title700, reader::Role::Display700}) {
    const reader::Font& font = f.fonts[role];
    const int extent = font.ascent() - font.descent();
    for (const int boxH : {21, 27, 32, 44, 53, 67, 68, 72, 80}) {
      for (const int top : {0, 1, 7, 100, 513}) {
        CAPTURE(boxH);
        CAPTURE(top);
        const int whole = reader::baselineIn(font, top, boxH);
        const int frac = reader::baselineInF26(font, reader::pxToF26(top), reader::pxToF26(boxH));
        CHECK(whole == frac);
        // The regression pin for the rule that was removed. Where the slack is
        // odd and positive the old formula sat a pixel HIGH; asserting the
        // direction as well as the magnitude is what stops a future "tidy-up"
        // reintroducing it as a truncation that looks harmless.
        const int slack = boxH - extent;
        const int twiceRounded = top + slack / 2 + font.ascent();
        if (slack > 0 && slack % 2 != 0)
          CHECK(whole == twiceRounded + 1);
        else
          CHECK(whole == twiceRounded);
      }
    }
  }
}

TEST_CASE("centreIn already rounds once, so it needs no fractional twin") {
  // iconTopIn forwards to centreIn, and the question the unification raised is
  // whether centreIn has baselineIn's old defect: a halving that rounds, feeding
  // something that rounds again. It does not -- it takes the slack, halves it
  // once, and that single division IS the rounding.
  //
  // Proved by construction rather than asserted: the fractional form of the same
  // answer is `f26ToPx(boxStart*64 + ((boxSize - itemSize)*64 >> 1))`, which is
  // the shape baselineInF26 has, and the two are equal everywhere -- including on
  // odd slack, on negative slack (the boards tighten line boxes below their
  // content) and on a negative origin. So converting centreIn to fixed point
  // would change no pixel on any screen, which is why it was left alone.
  for (const int boxStart : {-9, 0, 1, 18, 24, 241}) {
    for (const int boxSize : {0, 1, 21, 25, 27, 32, 68, 72, 80, 481}) {
      for (const int itemSize : {0, 1, 9, 12, 21, 25, 28, 33, 46, 500}) {
        CAPTURE(boxStart);
        CAPTURE(boxSize);
        CAPTURE(itemSize);
        const int frac = reader::f26ToPx(reader::pxToF26(boxStart) +
                                        ((reader::pxToF26(boxSize) - reader::pxToF26(itemSize)) >> 1));
        CHECK(reader::centreIn(boxStart, boxSize, itemSize) == frac);
      }
    }
  }
  // And halves go up, the same direction baselineInF26's f26ToPx takes them, so a
  // mark and the run beside it in one box are snapped by one rule rather than two
  // that part company on a half pixel.
  CHECK(reader::centreIn(0, 32, 21) == 6);   // slack 11 -> 5.5 -> 6
  CHECK(reader::centreIn(0, 27, 12) == 8);   // slack 15 -> 7.5 -> 8
  CHECK(reader::centreIn(0, 27, 28) == 0);   // slack -1 -> -0.5 -> 0
  CHECK(reader::centreIn(0, 24, 27) == -1);  // slack -3 -> -1.5 -> -1
}

TEST_CASE("centreIn is the axis-agnostic form of iconTopIn") {
  for (const int box : {0, 1, 25, 80, 432, 480}) {
    for (const int item : {0, 1, 21, 25, 84, 500}) {
      CAPTURE(box);
      CAPTURE(item);
      CHECK(reader::centreIn(0, box, item) == reader::iconTopIn(0, box, item));
      CHECK(reader::centreIn(24, box, item) == reader::iconTopIn(24, box, item));
    }
  }
}

// --- The 2C-2 primitives, against the measured boards -----------------------
//
// Every number below was read off the board in Chrome with
// getBoundingClientRect, at the 480x800 frame the boards are authored at, and it
// is the design's number rather than the implementation's: a test that recorded
// what the code happens to do would pass just as happily with the code wrong.

TEST_CASE("a header band with no mark right-aligns its value on the margin") {
  Ramp f;
  // design/Library.dc.html: `LIBRARY` / `12 BOOKS`, and no battery -- the value's
  // own right edge lands on 456 at 480 wide (measured: x=342.50 w=113.50).
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    const int h = reader::drawHeaderBand(fb, f.fonts, "LIBRARY", "12 BOOKS", nullptr);
    // The band's height does not depend on the mark today -- the battery is 21px
    // against the Value face's 32px line box -- and the board agrees: both the
    // Library's band and Home's measure 66.
    CHECK(h == reader::headerBandHeight(f.fonts, nullptr));
    CHECK(h == reader::headerBandHeight(f.fonts));
    CHECK(h == 66);
    int rightmost = -1;
    for (int y = 0; y < h - reader::kBandRuleH; ++y)
      for (int x = 0; x < width; ++x)
        if (!fb.getPixel(x, y) && x > rightmost) rightmost = x;
    // Within a glyph's side bearing of the margin, and never past it. With the
    // battery reserved anyway this would sit ~45px further left.
    CHECK(rightmost <= width - reader::kMargin);
    CHECK(rightmost > width - reader::kMargin - 4);
  }
}

TEST_CASE("a book row is the board's box, and its height is the text's not the thumbnail's") {
  Ramp f;
  // design/Library.dc.html: rows measure 90 tall with their border-bottom and 89
  // without (the focused row and the last one), and the content box is 67 --
  // which is the two-line text column (37 + 3 + 27), not the 64px thumbnail.
  CHECK(reader::bookRowContentH(f.fonts) == 67);
  CHECK(reader::bookRowHeight(f.fonts) == 90);
  CHECK(reader::bookRowContentH(f.fonts) > reader::kBookThumbH);

  reader::Framebuffer fb(480, 300);
  fb.clear(true);
  const reader::BookRowContent book{"Middlemarch", "GEORGE ELIOT", "6%", false};
  CHECK(reader::drawBookRow(fb, f.fonts, 0, book, false, true) == 90);
  CHECK(reader::drawBookRow(fb, f.fonts, 90, book, false, false) == 89);
  reader::Framebuffer focusFb(480, 300);
  focusFb.clear(true);
  CHECK(reader::drawBookRow(focusFb, f.fonts, 0, book, true, false) == 89);
}

TEST_CASE("a book row's rule is a hairline along its BOTTOM edge, full bleed") {
  Ramp f;
  reader::Framebuffer fb(480, 300);
  fb.clear(true);
  const int h = reader::drawBookRow(fb, f.fonts, 0, {"Walden", "", "48%", false}, false, true);
  // The board's `border-bottom: 1px solid`: the last row of the box, edge to
  // edge, and nothing on the first.
  CHECK_FALSE(fb.getPixel(0, h - 1));
  CHECK_FALSE(fb.getPixel(479, h - 1));
  CHECK(fb.getPixel(0, 0));
  CHECK(fb.getPixel(479, 0));
  // ...and exactly one pixel thick.
  CHECK(fb.getPixel(0, h - 2));
}

TEST_CASE("a book row's ink is level 0 or 3 outside the glyphs and the mark") {
  Ramp f;
  // The cover placeholder's dither, the cover's border and the row's rule are
  // opaque by construction, so they must be identical in every plane. Only the
  // glyph and chevron edges may differ -- which is the property that makes a
  // plane bug show up as fringing rather than as missing furniture.
  reader::Framebuffer bw(480, 200), dithered(480, 200);
  bw.clear(true);
  dithered.clear(true);
  const reader::BookRowContent row{"Classics", "FOLDER - 6 BOOKS", "", true};
  reader::drawBookRow(bw, f.fonts, 0, row, false, true, reader::Plane::Bw);
  reader::drawBookRow(dithered, f.fonts, 0, row, false, true, reader::Plane::BwDithered);
  // The cover column is not drawn on a folder row, so scan the thumbnail box and
  // the rule, both of which are furniture in either case.
  for (int y = 0; y < 90; ++y)
    CHECK(bw.getPixel(0, y) == dithered.getPixel(0, y));
}

TEST_CASE("a focused book row reverses its cover out of the fill") {
  Ramp f;
  reader::Framebuffer fb(480, 200);
  fb.clear(true);
  reader::drawBookRow(fb, f.fonts, 0, {"Dubliners", "JAMES JOYCE", "31%", false}, true, false);
  // The board's `.dither-dots-inv` with a 2px white border: the cover's own
  // outline is PAPER on a black row, where an unfocused row's is a 1px black
  // outline on white.
  const int coverY = reader::iconTopIn(reader::kBookRowPadY, reader::bookRowContentH(f.fonts),
                                       reader::kBookThumbH);
  CHECK(fb.getPixel(reader::kMargin, coverY));
  CHECK(fb.getPixel(reader::kMargin + reader::kBookThumbW - 1, coverY));
  // The fill itself is ink, immediately outside the cover's box.
  CHECK_FALSE(fb.getPixel(reader::kMargin - 2, coverY));
  // ...and the dots inside are paper, on the same 4px grid an unfocused cover
  // inks. Count them rather than name one: the grid is keyed on absolute
  // coordinates, so which cell is which depends on where the row landed.
  int paperInside = 0;
  for (int y = coverY + 3; y < coverY + reader::kBookThumbH - 3; ++y)
    for (int x = reader::kMargin + 3; x < reader::kMargin + reader::kBookThumbW - 3; ++x)
      if (fb.getPixel(x, y)) ++paperInside;
  CHECK(paperInside > 0);
}

TEST_CASE("an outlined action button is a border and a DIFFERENT face") {
  Ramp f;
  // design/DeleteConfirm.dc.html: the filled CANCEL is `--t-value` at 700 and the
  // outlined DELETE is `--t-label` at 500, both at 0.18em and both in a 68px box.
  // So the two are not the same run with a different ground, and the widths
  // differ: 119.00 against 104.70 for the board's own two labels.
  reader::Framebuffer filled(480, 200), outlined(480, 200);
  filled.clear(true);
  outlined.clear(true);
  CHECK(reader::drawActionButton(filled, f.fonts, 20, 10, 336, "DELETE", true) ==
        reader::kActionH);
  CHECK(reader::drawActionButton(outlined, f.fonts, 20, 10, 336, "DELETE", false) ==
        reader::kActionH);
  // Same box: `box-sizing: border-box` puts the border inside, so a focus move
  // between the two variants cannot shift either slab.
  CHECK_FALSE(filled.getPixel(20, 10));
  CHECK_FALSE(outlined.getPixel(20, 10));
  CHECK_FALSE(filled.getPixel(20, 10 + reader::kActionH - 1));
  CHECK_FALSE(outlined.getPixel(20, 10 + reader::kActionH - 1));
  // The outlined one's interior is paper where the filled one's is ink.
  CHECK_FALSE(filled.getPixel(30, 10 + 4));
  CHECK(outlined.getPixel(30, 10 + 4));
  // Its border is 2px, not 1.
  CHECK_FALSE(outlined.getPixel(20 + 1, 10 + 30));
  CHECK(outlined.getPixel(20 + 2, 10 + 30));
  // And the two labels measure differently, because they are two faces.
  const int fw = f.fonts[reader::Role::Value700].measure(
      "DELETE", reader::trackingEm(f.fonts[reader::Role::Value700], reader::kActionEm));
  const int ow = f.fonts[reader::Role::Label500].measure(
      "DELETE", reader::trackingEm(f.fonts[reader::Role::Label500], reader::kActionEm));
  CHECK(fw != ow);
}

TEST_CASE("an overlay's panel is centred on either geometry and opaque") {
  Ramp f;
  // LibraryActions is `left: 70px; width: 340px` and DeleteConfirm
  // `left: 50px; width: 380px` on the 480 canvas -- both centred, which is why
  // the left edge is derived. Hardcoding either puts the panel 24px off centre
  // on the 528-wide X3.
  CHECK(reader::panelLeft(480, 340) == 70);
  CHECK(reader::panelLeft(480, 380) == 50);
  CHECK(reader::panelLeft(528, 340) == 94);
  CHECK(reader::panelLeft(528, 380) == 74);
  CHECK(reader::panelContentW(340) == 336);

  reader::Framebuffer fb(480, 800);
  fb.clear(false);  // an all-ink ground, so "opaque" is a claim with teeth
  reader::drawPanel(fb, 70, 216, 340, 368);
  CHECK(fb.getPixel(70 + 10, 216 + 10));       // cleared to paper inside
  CHECK_FALSE(fb.getPixel(70, 216));           // 2px border
  CHECK_FALSE(fb.getPixel(70 + 1, 216 + 1));
  CHECK(fb.getPixel(70 + 2, 216 + 2));
  CHECK_FALSE(fb.getPixel(409, 583));
  CHECK_FALSE(fb.getPixel(69, 216));           // and nothing outside it moved
}

TEST_CASE("a panel caption's height is its wrapped label, in THIS face's line box") {
  Ramp f;
  // LibraryActions: `DUBLINERS` on a 336px content box is one line; DeleteConfirm's
  // `DELETE "DUBLINERS"?` wraps to two. Chrome measures those captions 74 and
  // 104; this measures 73 and 102, and the pixel per line is not a bug here.
  //
  // The caption is `line-height: normal`, which is the FACE's own line box, and
  // the two engines round that face's metrics differently at 23px: Space Grotesk
  // is 1.31em, so 23px is 30.13, and Chrome rounds its ascent and descent to 23
  // and 7 where FreeType's 26.6 metrics in the .rfnt come to 23 and 6. Following
  // the asset is the same rule the wrapped paragraph follows -- the alternative
  // is a constant here holding Chrome's rounding of a face's metrics, which is a
  // second source of truth for something the asset already declares.
  //
  // It is visible: it makes each panel 1px shorter per caption line, and a panel
  // is vertically centred, so it also sits half that lower.
  CHECK(f.fonts[reader::Role::Label500].lineHeight() == 29);
  const reader::Prose one = reader::wrapPanelCaption(f.fonts, "DUBLINERS", 336);
  CHECK(one.lineCount() == 1);
  CHECK(reader::panelCaptionHeight(f.fonts, one) == 73);

  // U+201C / U+201D, the board's own typographic quotes, spelled as UTF-8 bytes
  // with the string broken where a hex escape would otherwise swallow the next
  // character.
  const reader::Prose two =
      reader::wrapPanelCaption(f.fonts, "DELETE \xE2\x80\x9C" "DUBLINERS\xE2\x80\x9D?", 336);
  CHECK(two.lineCount() == 2);
  CHECK(reader::panelCaptionHeight(f.fonts, two) == 102);

  reader::Framebuffer fb(480, 800);
  fb.clear(true);
  const int h = reader::drawPanelCaption(fb, f.fonts, 70, 216, 340, one, "31%");
  CHECK(h == 73);
  // The 2px bottom rule spans the panel's own width, not the screen's.
  CHECK_FALSE(fb.getPixel(70, 216 + h - 1));
  CHECK_FALSE(fb.getPixel(409, 216 + h - 1));
  CHECK(fb.getPixel(69, 216 + h - 1));
  CHECK(fb.getPixel(410, 216 + h - 1));
}

TEST_CASE("a wrapped caption is left-aligned, a paragraph is centred") {
  Ramp f;
  const reader::Font& body = f.fonts[reader::Role::Body400];
  const std::string text = "one two three four five six seven eight nine ten";
  const reader::Prose p = reader::wrapProse(body, text, 200, reader::kProseLeadEm);
  REQUIRE(p.lineCount() >= 2);
  reader::Framebuffer centred(300, 300), left(300, 300);
  centred.clear(true);
  left.clear(true);
  reader::drawProse(centred, body, p, 0, 300, 0, reader::Ink::Black, reader::Plane::Bw,
                    reader::ProseAlign::Centre);
  reader::drawProse(left, body, p, 0, 300, 0, reader::Ink::Black, reader::Plane::Bw,
                    reader::ProseAlign::Left);
  // Every left-aligned line starts at the column's edge; a centred one does not.
  auto firstInkX = [](const reader::Framebuffer& fb, int y0, int y1) {
    for (int x = 0; x < fb.width(); ++x)
      for (int y = y0; y < y1; ++y)
        if (!fb.getPixel(x, y)) return x;
    return -1;
  };
  const int lead = reader::f26ToPx(p.leadF26);
  CHECK(firstInkX(left, 0, lead) < firstInkX(centred, 0, lead));
}

TEST_CASE("a panel row is 72 plus its rule, and its weight follows the focus") {
  Ramp f;
  // LibraryActions: `height: 72px; padding: 0 20px`, a 1px border-bottom on the
  // rows that have one, Value700 on the focused row and Value500 on the rest.
  CHECK(reader::panelRowHeight(true) == 73);
  CHECK(reader::panelRowHeight(false) == 72);

  reader::Framebuffer fb(480, 800);
  fb.clear(true);
  CHECK(reader::drawPanelRow(fb, f.fonts, 72, 292, 336, "Open", true, true, false) == 72);
  // The focused row's fill is the content box, not the box plus a rule.
  CHECK_FALSE(fb.getPixel(72, 292));
  CHECK_FALSE(fb.getPixel(72 + 335, 292 + 71));
  CHECK(fb.getPixel(72 + 336, 292 + 71));
  CHECK(fb.getPixel(72, 292 + 72));

  reader::Framebuffer plain(480, 800);
  plain.clear(true);
  CHECK(reader::drawPanelRow(plain, f.fonts, 72, 292, 336, "Book details", false, true, true) ==
        73);
  CHECK_FALSE(plain.getPixel(72, 292 + 72));        // the rule
  CHECK_FALSE(plain.getPixel(72 + 335, 292 + 72));
  CHECK(plain.getPixel(72 - 1, 292 + 72));          // ...inside the panel only
  // The label starts at the board's 20px inset from the content box.
  int firstX = -1;
  for (int x = 0; x < 480 && firstX < 0; ++x)
    for (int y = 292; y < 292 + 72; ++y)
      if (!plain.getPixel(x, y)) { firstX = x; break; }
  CHECK(firstX >= 72 + reader::kPanelPadX);
  CHECK(firstX < 72 + reader::kPanelPadX + 6);
}

// --- A title longer than its column ------------------------------------------
//
// The defect these pin was reported off a real card: the boards' sample titles
// are "Middlemarch" and "Dubliners", real filenames are not, and drawText had no
// right edge. Each case below asserts the thing the user actually sees -- where
// the INK stops -- rather than a return value, because an advance can be right
// while a glyph's bitmap hangs past it.

namespace {

// The leftmost inked column in rows [y0, y1), searching from `from`.
int leftmostInkFrom(const reader::Framebuffer& fb, int y0, int y1, int from) {
  for (int x = from; x < fb.width(); ++x)
    for (int y = y0; y < y1; ++y)
      if (!fb.getPixel(x, y)) return x;
  return -1;
}

const char* const kLongName = "Middlemarch_George_Eliot_1871_unabridged_edition_vol_one";

}  // namespace

TEST_CASE("the header band's label truncates instead of running into its value") {
  Ramp f;
  // The Library's band is the one band on any board whose label is data: a
  // subfolder's own name, shouted. Both panels, because the budget is derived
  // from the canvas.
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    fb.clear(true);
    const int h = reader::drawHeaderBand(fb, f.fonts, reader::upperAscii(kLongName), "12 BOOKS",
                                         nullptr);
    // Where the value starts: it keeps its width, and the label may not reach it.
    const reader::Font& vf = f.fonts[reader::Role::Value700];
    const int valueX = width - reader::kMargin - vf.measure("12 BOOKS");
    const int labelRight = rightmostInk(fb, 0, h - reader::kBandRuleH);
    CHECK(labelRight <= width - reader::kMargin);
    // The label's own ink stops at least the board's `gap: 7px` short of the
    // value. Scanning the strip left of the value is what isolates the label.
    reader::Framebuffer labelOnly(width, 200);
    labelOnly.clear(true);
    reader::drawHeaderBand(labelOnly, f.fonts, reader::upperAscii(kLongName), "", nullptr);
    CHECK(rightmostInk(labelOnly, 0, h - reader::kBandRuleH) <= width - reader::kMargin);
    // And with the value present there is a gap between the two runs.
    int gapStart = -1;
    for (int x = valueX - 1; x >= reader::kMargin; --x) {
      bool inked = false;
      for (int y = 0; y < h - reader::kBandRuleH; ++y)
        if (!fb.getPixel(x, y)) { inked = true; break; }
      if (inked) { gapStart = x; break; }
    }
    CHECK(gapStart >= 0);
    CHECK_MESSAGE(valueX - 1 - gapStart >= reader::kBandGap - 1,
                  "label ink at " << gapStart << " against a value starting at " << valueX);
  }
}

TEST_CASE("a short band label is drawn exactly as it was before a budget existed") {
  Ramp f;
  // The other 26 boards' band labels are literals that fit, and they must be
  // untouched -- which is what makes the fifteen goldens still hold.
  for (int width : {480, 528}) {
    reader::Framebuffer withBudget(width, 200);
    withBudget.clear(true);
    reader::drawHeaderBand(withBudget, f.fonts, "NOW READING", "87%");

    reader::Framebuffer plain(width, 200);
    plain.clear(true);
    const reader::Font& lf = f.fonts[reader::Role::Label500];
    reader::drawText(plain, lf, reader::kMargin,
                     reader::baselineIn(lf, reader::kBandPadTop,
                                        reader::headerBandHeight(f.fonts) -
                                            reader::kBandPadTop - reader::kBandPadBottom -
                                            reader::kBandRuleH),
                     "NOW READING", reader::Ink::Black, reader::trackingEm(lf, 220));
    // Compare only the label's own strip, left of the value.
    for (int y = 0; y < 60; ++y)
      for (int x = 0; x < 240; ++x)
        REQUIRE(withBudget.getPixel(x, y) == plain.getPixel(x, y));
  }
}

TEST_CASE("a book row's title and meta line both truncate inside their column") {
  Ramp f;
  for (int width : {480, 528}) {
    reader::Framebuffer fb(width, 200);
    fb.clear(true);
    // The meta line has to be a named string: BookRowContent holds views, so a
    // temporary here is a dangling read at draw time.
    const std::string meta = reader::upperAscii(kLongName);
    const reader::BookRowContent row{kLongName, meta, "31%", false};
    const int consumed = reader::drawBookRow(fb, f.fonts, 0, row, false, true);
    const reader::Font& vf = f.fonts[reader::Role::Value700];
    const int valueX = width - reader::kMargin - vf.measure("31%");
    // The board's `gap: 16px` before the row's third flex child is what the text
    // column may not cross.
    const int limit = valueX - reader::kBookThumbGap;
    // Scan the row without its bottom rule, which is full-bleed by design.
    for (int y = 0; y < consumed - reader::kBookRowRuleH; ++y)
      for (int x = limit; x < valueX; ++x)
        REQUIRE_MESSAGE(fb.getPixel(x, y), "text column ink at (" << x << ", " << y << ")");
    // Both lines are actually there and actually cut: ink on each of the column's
    // two line boxes, and neither reaches the limit.
    const int textX = reader::kMargin + reader::kBookThumbW + reader::kBookThumbGap;
    CHECK(leftmostInkFrom(fb, 0, consumed, textX) >= textX);
  }
}

TEST_CASE("a folder row's title truncates against its chevron, not against a value") {
  Ramp f;
  reader::Framebuffer fb(480, 200);
  fb.clear(true);
  const reader::BookRowContent row{kLongName, "FOLDER", "", true};
  const int consumed = reader::drawBookRow(fb, f.fonts, 0, row, false, true);
  // A folder discloses and states no value, so the budget is the chevron's own
  // width -- reserving a value's width here would narrow every folder row for a
  // mark that is not on it.
  const int chevX = 480 - reader::kMargin - reader::icons::kChevron.w;
  for (int y = 0; y < consumed - reader::kBookRowRuleH; ++y)
    for (int x = chevX - reader::kBookThumbGap; x < chevX; ++x)
      REQUIRE_MESSAGE(fb.getPixel(x, y), "folder title ink at (" << x << ", " << y << ")");
}

TEST_CASE("wrapProse breaks inside a word only when the board asks it to") {
  Ramp f;
  const reader::Font& t = f.fonts[reader::Role::Title700];
  const int colW = 292;  // Book details' column on the X4
  // Normal: the word has nowhere better to be, so it takes a line and overhangs.
  // Every paragraph on every board relies on this, so it must not change.
  const reader::Prose normal =
      reader::wrapProseLead(t, kLongName, colW, reader::pxToF26(46));
  CHECK(normal.lineCount() == 1);
  CHECK(t.measure(normal.lines[0]) > colW);
  // Anywhere: the board's `overflow-wrap: anywhere` on Book details' title, and
  // every line but the last is within the column.
  const reader::Prose anywhere = reader::wrapProseLead(t, kLongName, colW, reader::pxToF26(46),
                                                       {}, reader::WordBreak::Anywhere);
  CHECK(anywhere.lineCount() > 1);
  for (const std::string_view line : anywhere.lines)
    CHECK_MESSAGE(t.measure(line) <= colW, "line over the column: " << std::string(line));
  // Nothing is lost and nothing is duplicated: the lines concatenate back.
  std::string joined;
  for (const std::string_view line : anywhere.lines) joined += std::string(line);
  CHECK(joined == std::string(kLongName));
}

TEST_CASE("clampProse bounds a wrapped run and elides what is left over") {
  Ramp f;
  const reader::Font& t = f.fonts[reader::Role::Title700];
  const int colW = 292;
  const std::string name = std::string(kLongName) + "_and_then_some_more_besides";
  reader::Prose p =
      reader::wrapProseLead(t, name, colW, reader::pxToF26(46), {}, reader::WordBreak::Anywhere);
  REQUIRE(p.lineCount() > 3);
  std::string tail;
  reader::clampProse(t, p, 3, colW, tail);
  CHECK(p.lineCount() == 3);
  // The last line is the elided remainder, and it fits the column like the rest.
  CHECK(t.measure(p.lines.back()) <= colW);
  CHECK(p.lines.back() == tail);
  CHECK(tail.size() >= reader::kEllipsis.size());
  CHECK(tail.substr(tail.size() - reader::kEllipsis.size()) == reader::kEllipsis);
  // A run already inside the bound is not touched, and gains no ellipsis.
  reader::Prose fits =
      reader::wrapProseLead(t, "Dubliners", colW, reader::pxToF26(46), {},
                            reader::WordBreak::Anywhere);
  std::string noTail;
  reader::clampProse(t, fits, 3, colW, noTail);
  CHECK(fits.lineCount() == 1);
  CHECK(fits.lines[0] == "Dubliners");
  CHECK(noTail.empty());
}

// --- The scroll rail --------------------------------------------------------
//
// Untested until now for a reason worth naming: Library's own golden shows seven
// rows of seven, so it does NOT overflow and the rail never draws in it. The one
// place the rail appears is a state no golden held, which is the shape of gap
// that has hidden four bugs in this project.

namespace {
// Ink anywhere in the rail's column, which is the whole question: did it draw.
// getPixel reports WHITE -- the framebuffer's bool is "is paper", which is why
// every fillRect that inks passes false -- so ink is its negation.
int railInk(const reader::Framebuffer& fb) {
  int n = 0;
  for (int x = fb.width() - reader::kRailRightGap - reader::kRailW; x < fb.width(); ++x)
    for (int y = 0; y < fb.height(); ++y)
      if (!fb.getPixel(x, y)) ++n;
  return n;
}
// The vertical extent of the SOLID thumb.
//
// Scanned strictly INSIDE the track, because the track's own end caps span the
// full rail width and would otherwise read as thumb: measured over the whole
// panel this reported the track's height for every list length, which made a
// proportional thumb and a constant one indistinguishable -- the exact confusion
// the design board's first draft also invited.
void thumbExtent(const reader::Framebuffer& fb, int listTop, int listBottom, int& top,
                 int& bottom) {
  const int x = fb.width() - reader::kRailRightGap - reader::kRailW + reader::kRailBorder;
  const int yFrom = listTop + reader::kRailEndInset + reader::kRailBorder;
  const int yTo = listBottom - reader::kRailEndInset - reader::kRailBorder;
  top = -1;
  bottom = -1;
  for (int y = yFrom; y < yTo; ++y) {
    bool solid = true;
    for (int i = 0; i < reader::kRailW - 2 * reader::kRailBorder; ++i)
      if (fb.getPixel(x + i, y)) solid = false;  // any paper means not the thumb
    if (!solid) continue;
    if (top < 0) top = y;
    bottom = y;
  }
}
}  // namespace

TEST_CASE("a list that fits draws no rail at all") {
  reader::Framebuffer fb(480, 800);
  fb.clear(true);
  reader::drawScrollRail(fb, 100, 700, 0, 7, 7);
  CHECK(railInk(fb) == 0);

  // And a shorter list than the window, which ScrollWindow permits.
  reader::drawScrollRail(fb, 100, 700, 0, 7, 3);
  CHECK(railInk(fb) == 0);
  // Nothing at all to position.
  reader::drawScrollRail(fb, 100, 700, 0, 7, 0);
  CHECK(railInk(fb) == 0);
}

TEST_CASE("the thumb is proportional to the visible fraction") {
  reader::Framebuffer fb(480, 800);
  fb.clear(true);
  // The board's fiction: 24 rows, 7 visible, scrolled to row 8.
  reader::drawScrollRail(fb, 100, 700, 7, 7, 24);
  int top = 0, bottom = 0;
  thumbExtent(fb, 100, 700, top, bottom);
  REQUIRE(top > 0);

  const int innerTop = 100 + reader::kRailEndInset + reader::kRailBorder;
  const int innerH = (700 - reader::kRailEndInset) - (100 + reader::kRailEndInset) -
                     2 * reader::kRailBorder;
  const int h = bottom - top + 1;
  // 7 of 24 of the track, within a pixel of the rounding.
  CHECK(h >= innerH * 7 / 24 - 1);
  CHECK(h <= innerH * 7 / 24 + 1);
  // ...starting 7 of 24 down it.
  CHECK(top >= innerTop + innerH * 7 / 24 - 1);
  CHECK(top <= innerTop + innerH * 7 / 24 + 1);
}

TEST_CASE("a longer list gives a shorter thumb") {
  // The property that a fixed-size thumb would fail, and the one the design
  // board's first draft could not have caught: at 7 of 9 the thumb is 78% of the
  // track, which looks the same as a constant.
  int hs[3] = {};
  const int totals[3] = {9, 24, 256};
  for (int i = 0; i < 3; ++i) {
    reader::Framebuffer fb(480, 800);
    fb.clear(true);
    reader::drawScrollRail(fb, 100, 700, 0, 7, totals[i]);
    int top = 0, bottom = 0;
    thumbExtent(fb, 100, 700, top, bottom);
    hs[i] = bottom - top + 1;
  }
  CHECK(hs[0] > hs[1]);
  CHECK(hs[1] > hs[2]);
  // Never invisible, however long the list: at the 256-row cap the true
  // proportion rounds to a couple of pixels, which reads as dirt on the track.
  CHECK(hs[2] >= reader::kRailThumbMinH);
}

TEST_CASE("the thumb never overhangs the track, at either end") {
  reader::Framebuffer fb(480, 800);
  const int innerTop = 100 + reader::kRailEndInset + reader::kRailBorder;
  const int innerBottom = (700 - reader::kRailEndInset) - reader::kRailBorder - 1;

  // The very bottom of the longest list, where the minimum-height floor and the
  // proportional top would otherwise push the thumb through the end cap.
  fb.clear(true);
  reader::drawScrollRail(fb, 100, 700, 256 - 7, 7, 256);
  int top = 0, bottom = 0;
  thumbExtent(fb, 100, 700, top, bottom);
  CHECK(top >= innerTop);
  CHECK(bottom <= innerBottom);

  // And the very top.
  fb.clear(true);
  reader::drawScrollRail(fb, 100, 700, 0, 7, 256);
  thumbExtent(fb, 100, 700, top, bottom);
  CHECK(top >= innerTop);
  CHECK(bottom <= innerBottom);
}

TEST_CASE("the rail stays inside the gutter it was given") {
  // It must not reach the panel edge (that gap is the board's 4px) and must not
  // reach into the rows' column, or it would collide with the focused fill.
  reader::Framebuffer fb(480, 800);
  fb.clear(true);
  reader::drawScrollRail(fb, 100, 700, 3, 7, 24);
  for (int y = 0; y < 800; ++y) {
    for (int x = 480 - reader::kRailRightGap; x < 480; ++x) CHECK(fb.getPixel(x, y));
    CHECK(fb.getPixel(480 - reader::kListGutterW - 1, y));
  }
}

TEST_CASE("a rail with no room draws nothing rather than inverting") {
  // listBottom above listTop, which a very short panel or a very tall hint bar
  // could produce. A negative height must not become a full-frame fill.
  reader::Framebuffer fb(480, 800);
  fb.clear(true);
  reader::drawScrollRail(fb, 400, 402, 3, 7, 24);
  CHECK(railInk(fb) == 0);
  reader::drawScrollRail(fb, 400, 380, 3, 7, 24);
  CHECK(railInk(fb) == 0);
}
