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
    const int barW = width - 2 * reader::kMargin;
    const int probeX = reader::kMargin + 5;
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
    const int right = reader::kMargin + barW;
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
    // Right-aligned on the block's own `padding: 0 20px`, measured off the mark
    // the board actually puts there.
    CHECK(x1 <= right - 20);
    CHECK(x1 >= right - 20 - 3);
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
