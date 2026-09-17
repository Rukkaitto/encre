#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "home_vm.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"
#include "reader/screen_home.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

using ramp::Ramp;

TEST_CASE("QuietTheme renders Home to golden on both panel geometries") {
  Ramp ramp;
  reader::FontSet& fonts = ramp.fonts;
  reader::QuietTheme theme;

  // Home takes the default Fidelity::Mono, so the golden is the single 1-bit
  // frame the panel is handed -- one pass with Plane::Bw, exactly what
  // paintMono() in the shell and the simulator render. Glyph and icon edges are
  // hard-thresholded, which is what the reference firmware does to its chrome;
  // rules, fills and the cover's dither have coverage 0 or 3 and so are
  // unchanged from the stippled frame this golden used to hold.
  //
  // The fidelity is asserted, not assumed: this test names a plane, and if the
  // screen's declared path ever moves the golden must stop matching rather than
  // quietly keep pinning a path nothing paints.
  REQUIRE(reader::HomeScreen(sampleHome(), {}).fidelity() == reader::Fidelity::Mono);
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    const reader::HomeViewModel vm = sampleHome();
    theme.renderHome(fb, fonts, vm, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  SUBCASE("X4 480x800") { renderOne(480, 800, "home_quiet"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "home_quiet_x3"); }
}

// design/HomeUnopened.dc.html, pixel-exact at both geometries.
//
// THE NO-READING-COLUMN LAYOUT HAD NO GOLDEN AT ALL until this. `home_empty` has
// been compared only by `make compare`, which renders both sides fresh and so
// cannot notice the two drifting together -- and this layout is the one with the
// most arithmetic on the screen: a centred 112px mark, a centred title and a
// wrapped paragraph, all accumulated in 1/64 px because the prose's height is a
// fraction (1.55 x 29px = 44.95) and rounding it early would move everything under
// it. That is precisely the kind of code a golden is for.
TEST_CASE("QuietTheme renders Home with nothing open to golden on both geometries") {
  Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, ramp.fonts, reader::demoHomeUnopenedVm(), reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "home_unopened"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "home_unopened_x3"); }
}

// design/Main.dc.html's column inset and the CONTINUE block's own padding. Named
// here rather than reached for from the theme: they are private to theme_quiet,
// and a test that read them from there could not catch the theme changing them.
static constexpr int kSpineColPad = 20;
static constexpr int kSlabPadX = 18;

TEST_CASE("Home's action block carries the long arrow, not the row chevron") {
  // design/Main.dc.html:63 puts a 32x25 shafted arrow in the CONTINUE block;
  // :74 puts a 25x25 chevron on the SETTINGS row. They are different marks for
  // different jobs, and the block drew the chevron. Asserted on the render
  // rather than on the drawIcon argument, because what was wrong was the pixels.
  Ramp ramp;
  reader::QuietTheme theme;
  for (int width : {480, 528}) {
    const int height = width == 480 ? 800 : 792;
    reader::Framebuffer fb(width, height);
    reader::HomeViewModel vm = sampleHome();
    theme.renderHome(fb, ramp.fonts, vm, reader::Plane::Bw);

    // Find the block: the only run of rows filled solid black across the usable
    // width. CONTINUE is focused in this view model, so the mark is knocked out
    // in white and the block's own field is the surrounding ink.
    // The longest unbroken run of ink in a column just inside the left margin:
    // the block's field is solid there (its own `padding: 0 20px` keeps the
    // label clear of it), while the cover's dither and the progress bar's fill
    // are only a few rows deep.
    // INSIDE THE SLAB, NOT INSIDE THE SPINE. This probed `kMargin + 5` = 29,
    // which the spine now owns -- so the longest run of ink in that column is
    // the BAND (~736 rows) rather than the 68px block, and every measurement
    // after it described the wrong object. The slab lives in the column beside
    // the band, so the probe has to start there.
    const int barW = width - reader::kMargin - (reader::kSpineW + kSpineColPad);
    const int probeX = reader::kSpineW + kSpineColPad + 5;
    int blockTop = -1, blockBot = -2, runTop = -1;
    for (int y = 0; y <= height; ++y) {
      const bool solid = y < height && !fb.getPixel(probeX, y);
      if (solid && runTop < 0) runTop = y;
      if (!solid && runTop >= 0) {
        if (y - runTop > blockBot - blockTop) {
          blockTop = runTop;
          blockBot = y - 1;
        }
        runTop = -1;
      }
    }
    REQUIRE(blockBot - blockTop >= 40);

    // The knocked-out mark, in the block's right-hand end.
    const int right = reader::kSpineW + kSpineColPad + barW;
    int x0 = right, x1 = -1, y0 = height, y1 = -1;
    for (int y = blockTop; y <= blockBot; ++y)
      for (int x = right - 120; x < right; ++x)
        if (fb.getPixel(x, y)) {
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (y < y0) y0 = y;
          if (y > y1) y1 = y;
        }
    REQUIRE(x1 > 0);
    // 32 wide, not 25: the chevron cannot span this.
    CHECK(x1 - x0 + 1 > reader::icons::kChevron.w);
    CHECK(x1 - x0 + 1 <= reader::icons::kForward.w);
    // And it has a shaft: its middle row is inked nearly edge to edge, where a
    // chevron's carries only its vertex.
    int mid = 0;
    for (int x = x0; x <= x1; ++x)
      if (fb.getPixel(x, (y0 + y1) / 2)) ++mid;
    CHECK(mid >= (x1 - x0 + 1) * 3 / 4);
    // Right-aligned on the block's own `padding: 0 18px` -- 18 where the old
    // full-width slab used 20, because design/Main.dc.html insets the narrower
    // block by its own padding rather than the screen's.
    CHECK(x1 <= right - kSlabPadX);
    CHECK(x1 >= right - kSlabPadX - 3);
  }
}

TEST_CASE("the charging battery is the same box as the idle one") {
  // The band's height is derived from the mark (headerBandHeight takes it), and
  // the number's position is derived from the mark's width. If the two states
  // differed in either, swapping them would move the band and every row under
  // it -- which is the header-band defect this project has already paid for once.
  CHECK(reader::icons::kBatteryCharging.w == reader::icons::kBattery.w);
  CHECK(reader::icons::kBatteryCharging.h == reader::icons::kBattery.h);
  // Not the same BYTES, though: that would mean the bolt never reached the
  // asset. Comparing `.rows` itself is a pointer comparison and can never be
  // equal whatever the two arrays hold, so this compares the CONTENTS.
  const int stride = (reader::icons::kBatteryCharging.w * reader::icons::kBatteryCharging.bpp + 7) / 8;
  CHECK(std::memcmp(reader::icons::kBatteryCharging.rows, reader::icons::kBattery.rows,
                     static_cast<size_t>(stride) * reader::icons::kBatteryCharging.h) != 0);
}

namespace {
// Ink in a rectangle. getPixel reports WHITE, so ink is its negation.
int inkIn(const reader::Framebuffer& fb, int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0; x < x0 + w; ++x)
      if (!fb.getPixel(x, y)) ++n;
  return n;
}
}  // namespace

TEST_CASE("an unknown charge draws the mark alone, with the mark still on the margin") {
  Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, int percent) {
    reader::Framebuffer fb(w, h);
    reader::HomeViewModel vm = sampleHome();
    vm.batteryPercent = percent;
    theme.renderHome(fb, ramp.fonts, vm, reader::Plane::Bw);
    return fb;
  };
  for (const auto geom : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    const int w = geom.first, h = geom.second;
    const reader::Framebuffer known = renderOne(w, h, 87);
    const reader::Framebuffer unknown = renderOne(w, h, -1);

    const int iconLeft = w - reader::kMargin - reader::icons::kBattery.w;
    // headerBandHeight() includes the band's own trailing rule (kBandRuleH), a
    // full-width fillRect drawn identically whatever the charge string is. A
    // window that reaches it picks up that rule's ink in EVERY case, known or
    // not -- so the content-only checks below stop short of it.
    const int bandH = reader::headerBandHeight(ramp.fonts, &reader::icons::kBattery) -
                       reader::kBandRuleH;

    // The mark itself is drawn in BOTH, in the same place: the number going away
    // must not move it off the margin.
    CHECK(inkIn(known, iconLeft, 0, reader::icons::kBattery.w, bandH) > 0);
    CHECK(inkIn(unknown, iconLeft, 0, reader::icons::kBattery.w, bandH) ==
          inkIn(known, iconLeft, 0, reader::icons::kBattery.w, bandH));

    // The 20 columns where the number's last glyph would land: inked when the
    // charge is known, blank when it is not.
    const int numberX = iconLeft - reader::kBandGap - 20;
    CHECK(inkIn(known, numberX, 0, 20, bandH) > 0);
    CHECK(inkIn(unknown, numberX, 0, 20, bandH) == 0);

    // BOTH AXES ARE INDEPENDENT, so "no percentage but charging" is reachable:
    // BatteryTracker records percentKnown and chargingKnown separately, and a
    // gauge really can answer one and fail the other. Bolt, and still no digits.
    reader::HomeViewModel vm = sampleHome();
    vm.batteryPercent = -1;
    vm.batteryCharging = true;
    reader::Framebuffer both(w, h);
    theme.renderHome(both, ramp.fonts, vm, reader::Plane::Bw);
    CHECK(inkIn(both, numberX, 0, 20, bandH) == 0);
    // The mark is the CHARGING one: its columns differ from the idle render's.
    int differing = 0;
    for (int y = 0; y < bandH; ++y)
      for (int x = iconLeft; x < iconLeft + reader::icons::kBattery.w; ++x)
        if (both.getPixel(x, y) != unknown.getPixel(x, y)) ++differing;
    CHECK(differing > 0);
  }
}

TEST_CASE("charging swaps the mark and nothing else") {
  Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](bool charging) {
    reader::Framebuffer fb(480, 800);
    reader::HomeViewModel vm = sampleHome();
    vm.batteryCharging = charging;
    theme.renderHome(fb, ramp.fonts, vm, reader::Plane::Bw);
    return fb;
  };
  const reader::Framebuffer idle = renderOne(false);
  const reader::Framebuffer charging = renderOne(true);

  const int iconLeft = 480 - reader::kMargin - reader::icons::kBattery.w;
  int differing = 0, differingOutsideMark = 0;
  for (int y = 0; y < 800; ++y)
    for (int x = 0; x < 480; ++x)
      if (idle.getPixel(x, y) != charging.getPixel(x, y)) {
        ++differing;
        if (x < iconLeft || x >= iconLeft + reader::icons::kBattery.w) ++differingOutsideMark;
      }
  // The bolt is a real, visible difference...
  CHECK(differing > 0);
  // ...and it is confined to the mark's own columns. If anything outside them
  // moved, the two marks are not the same box and the band has shifted.
  CHECK(differingOutsideMark == 0);
}

// design/HomeCharging.dc.html, pixel-exact at both geometries. The charging mark
// is the one thing here nothing else pins: the unit tests above assert the two
// marks share a box and that the swap is confined to the mark's columns, which is
// structure -- this is what says the bolt actually renders as a bolt.
TEST_CASE("QuietTheme renders Home charging to golden on both geometries") {
  Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    reader::HomeViewModel vm = sampleHome();
    vm.batteryCharging = true;
    theme.renderHome(fb, ramp.fonts, vm, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "home_charging"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "home_charging_x3"); }
}

namespace {
// The bar's height is ASKED FOR rather than assumed, exactly as renderHome asks
// for it: it derives from the hint labels and their marks, so a bar whose first
// slot is empty is not necessarily the same height as one that is not.
int homeBarH(const reader::FontSet& fonts, const reader::HomeViewModel& vm) {
  reader::Hint hints[4];
  const reader::Icon* marks[4] = {&reader::icons::kBook, &reader::icons::kDot,
                                  &reader::icons::kUp, &reader::icons::kDown};
  reader::buildHints(marks, vm.hints, vm.holds, hints);
  return reader::hintBarHeight(fonts, hints);
}
}  // namespace

// --- The missing book -------------------------------------------------------
//
// design/HomeMissing.dc.html: the pointer names a book the card no longer has.
// The reading column stays -- the pointer knows the name, the author, the
// percentage and the chapter -- with a bordered strip over it and no CONTINUE
// slab. A golden at both geometries, because this layout is the only one on Home
// whose HEIGHT is a result: the note wraps, so the strip's box grows with it and
// everything under it moves.
TEST_CASE("QuietTheme renders Home's missing book to golden on both geometries") {
  Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, ramp.fonts, reader::demoHomeMissingVm(), reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "home_missing"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "home_missing_x3"); }
}

TEST_CASE("the missing-book strip draws no CONTINUE slab, and the ordinary Home does") {
  // The slab is the largest run of solid ink in the column, so its absence is
  // measurable without knowing where it sat: with it gone, the column below the
  // stats is paper all the way to the menu.
  Ramp ramp;
  reader::QuietTheme theme;
  for (const auto geom : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    const int w = geom.first, h = geom.second;
    reader::Framebuffer ordinary(w, h), missing(w, h);
    theme.renderHome(ordinary, ramp.fonts, reader::demoHomeVm(), reader::Plane::Bw);
    theme.renderHome(missing, ramp.fonts, reader::demoHomeMissingVm(), reader::Plane::Bw);
    const int barH = homeBarH(ramp.fonts, reader::demoHomeMissingVm());

    // ROWS INKED ALL THE WAY ACROSS THE COLUMN, which is what a slab is and what no
    // run of type can be. A probe at one x is not enough: the first version used
    // one five pixels into the column and measured 43 unbroken rows on the missing
    // render -- the `6` of the 67px numeral, which starts on the same margin.
    const int x0 = reader::kSpineW + kSpineColPad;
    const int x1 = w - reader::kMargin;
    auto fullRows = [&](const reader::Framebuffer& fb, int yEnd) {
      int n = 0;
      for (int y = 0; y < yEnd; ++y) {
        bool full = true;
        for (int x = x0; x < x1 && full; ++x) full = !fb.getPixel(x, y);
        if (full) ++n;
      }
      return n;
    };
    // The menu's focused row is full-bleed inverted, so the count stops above it.
    const int menuTop = h - barH - 3 * reader::kRowH;
    // CONTINUE is 68 tall and FILLED, because demoHomeVm focuses it -- less the
    // rows its knocked-out label and arrow interrupt, which is why this is not 68.
    CHECK(fullRows(ordinary, menuTop) >= 30);
    // The missing state's only full-column rows are the strip's two 2px borders.
    CHECK(fullRows(missing, menuTop) == 2 * 2);
  }
}

TEST_CASE("A LONG TITLE CANNOT PUSH THE STATS INTO THE MENU") {
  // The note carries a name off the CARD, so it is the one run in this column that
  // can grow -- and everything below it is fixed and the menu is bottom-anchored,
  // which is what makes a budget derivable at all. Unbounded, a 255-byte name (FAT's
  // long-name maximum, so a name a real card can hold) wraps to about fourteen lines
  // and writes the author, the numeral and the chapter over the menu rows.
  Ramp ramp;
  reader::QuietTheme theme;
  // A title with spaces, so the wrap has real break opportunities: `Anywhere` would
  // hide a missing clamp behind its own breaking on a single long word.
  std::string longTitle;
  while (longTitle.size() < 255) longTitle += "Middlemarch or a Study of Provincial Life ";
  longTitle.resize(255);

  for (const auto geom : {std::pair<int, int>{480, 800}, std::pair<int, int>{528, 792}}) {
    const int w = geom.first, h = geom.second;
    reader::HomeViewModel shortVm = reader::demoHomeMissingVm();
    reader::HomeViewModel longVm = shortVm;
    longVm.title = longTitle;
    longVm.missingNote = reader::missingBookNote(longVm.title);

    reader::Framebuffer a(w, h), b(w, h);
    theme.renderHome(a, ramp.fonts, shortVm, reader::Plane::Bw);
    theme.renderHome(b, ramp.fonts, longVm, reader::Plane::Bw);

    // THE MENU AND THE BAR ARE BYTE-IDENTICAL, which is the whole property: they
    // are anchored to the bottom and know nothing about the title, so anything
    // differing down there is the column having overrun into them. The spine is
    // excluded because it legitimately carries the two different names.
    const int menuTop = h - homeBarH(ramp.fonts, shortVm) - 3 * reader::kRowH;
    int differing = 0;
    for (int y = menuTop; y < h; ++y)
      for (int x = reader::kSpineW; x < w; ++x)
        if (a.getPixel(x, y) != b.getPixel(x, y)) ++differing;
    CHECK(differing == 0);

    // ...AND THE CLAMP ACTUALLY ENGAGED, so the case above cannot pass by the note
    // having stayed one line. The box's bottom border is a full-column horizontal
    // rule; the long note's sits lower than the short one's and still above the
    // menu. Without that second half the fixture could go quiet the day the budget
    // is derived wrongly and answers 1.
    auto boxBottom = [&](const reader::Framebuffer& fb) {
      const int x0 = reader::kSpineW + kSpineColPad;
      const int x1 = w - reader::kMargin;
      int last = -1;
      for (int y = 0; y < menuTop; ++y) {
        bool full = true;
        for (int x = x0; x < x1 && full; ++x) full = !fb.getPixel(x, y);
        if (full) last = y;
      }
      return last;
    };
    const int shortBottom = boxBottom(a);
    const int longBottom = boxBottom(b);
    REQUIRE(shortBottom > 0);
    CHECK(longBottom > shortBottom);
    CHECK(longBottom < menuTop);
  }
}
