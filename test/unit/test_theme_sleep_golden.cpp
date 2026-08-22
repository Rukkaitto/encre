// Sleep had NO golden, and that is how its progress bar stayed wrong.
//
// The screen is implemented in the theme and reachable in the simulator, but
// nothing pinned its pixels -- so when the bar was drawn over its own border at
// full height instead of inset inside it, as design/Sleep.dc.html declares with
// `box-sizing: border-box`, every test in the suite still passed. Home's four
// goldens caught the same defect on Home in six pixels. This is the pair Sleep
// was missing.
#include <string>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_sleep.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

namespace {

// design/Sleep.dc.html's own copy, verbatim -- the board shouts the title and the
// author itself, so the view model carries them cased as a book's metadata
// arrives and the theme does the shouting.
reader::SleepViewModel sampleSleep() {
  reader::SleepViewModel vm;
  vm.label = "NOW READING";
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.progressPercent = 6;
  vm.progress = "6% \xC2\xB7 CH. 01";
  vm.note = "ASLEEP \xC2\xB7 PRESS POWER TO WAKE";
  return vm;
}

}  // namespace

TEST_CASE("QuietTheme renders Sleep to golden on both panel geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  // Pinned like Home's: if the screen's declared fidelity moves, the golden must
  // stop matching rather than quietly keep pinning a path nothing paints.
  REQUIRE(reader::SleepScreen(sampleSleep()).fidelity() == reader::Fidelity::Mono);
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, sampleSleep(), reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "sleep_quiet"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "sleep_quiet_x3"); }
}

TEST_CASE("SLEEP'S BAR FILL IS INSIDE ITS BORDER, as box-sizing: border-box says") {
  // The defect itself, asserted on geometry rather than on the whole image, so a
  // future re-bless cannot quietly take it back. The board is 170x8 with a 1px
  // border, so the fill occupies rows y+1..y+6 and NEVER the border rows.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  reader::Framebuffer fb(480, 800);
  reader::SleepViewModel vm = sampleSleep();
  vm.progressPercent = 50;
  theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw);

  // Find the bar: the widest horizontal black run that is exactly 170px long.
  int barY = -1, barX = -1;
  for (int y = 0; y < fb.height() && barY < 0; ++y) {
    int run = 0;
    for (int x = 0; x < fb.width(); ++x) {
      // getPixel reports WHITE (see framebuffer.h), so black is its negation.
      if (!fb.getPixel(x, y)) {
        ++run;
        if (run == 170 && (x + 1 >= fb.width() || fb.getPixel(x + 1, y))) {
          barY = y;
          barX = x - 169;
        }
      } else {
        run = 0;
      }
    }
  }
  REQUIRE(barY >= 0);

  // The row just inside the top border, at 50%: 50% of the 168px content box is
  // 84px of fill, then white to the right border.
  const int mid = barY + 4;
  int fill = 0;
  for (int x = barX + 1; x < barX + 169; ++x) {
    if (fb.getPixel(x, mid)) break;  // white: the fill ended
    ++fill;
  }
  CHECK(fill == 84);
  // And the pixel immediately left of the right border is white -- the fill did
  // not run to the edge.
  CHECK(fb.getPixel(barX + 168, mid));
}
