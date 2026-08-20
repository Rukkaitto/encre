#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static reader::HomeViewModel sampleHome() {
  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}

using ramp::Ramp;

TEST_CASE("QuietTheme renders Home to golden on both panel geometries") {
  Ramp ramp;
  reader::FontSet& fonts = ramp.fonts;
  reader::QuietTheme theme;

  // Home goes through the grayscale path: three passes, and the golden holds
  // the 4-level composition of the two planes. The Bw pass is rendered too, so
  // the test drives the same sequence the shell and the simulator do.
  auto renderThree = [&](int w, int h, const std::string& name) {
    reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
    const reader::HomeViewModel vm = sampleHome();
    theme.renderHome(bw, fonts, vm, reader::Plane::Bw);
    theme.renderHome(lsb, fonts, vm, reader::Plane::Lsb);
    theme.renderHome(msb, fonts, vm, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderThree(480, 800, "home_quiet"); }
  SUBCASE("X3 528x792") { renderThree(528, 792, "home_quiet_x3"); }
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
