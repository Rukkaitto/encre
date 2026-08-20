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
      reader::drawHeaderBand(fb, f.fonts, "NOW READING", "87%", plane);
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
