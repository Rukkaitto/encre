#include <fstream>
#include <string>
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
