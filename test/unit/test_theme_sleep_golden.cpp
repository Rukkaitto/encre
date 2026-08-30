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
#include "reader/screens.h"
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
    theme.renderSleep(fb, ramp.fonts, sampleSleep(), reader::Plane::Bw, nullptr);
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
  theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);

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

// --- Asleep with nothing open --------------------------------------------------
//
// design/SleepIdle.dc.html. The card IS the reading state, and the device sleeps from
// Home or the Library as often as from a book -- so with nothing open the badge is
// drawn alone. It is the badge that carries this screen's purpose: e-ink holds its
// last image, so without it a Library left on the glass gives no clue the device is
// asleep rather than frozen.

TEST_CASE("QuietTheme renders the idle Sleep screen to golden on both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, reader::demoSleepIdleVm(), reader::Plane::Bw, nullptr);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "sleep_idle"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "sleep_idle_x3"); }
}

TEST_CASE("the idle Sleep screen draws the badge and NOTHING where the card was") {
  // Asserted against the reading render rather than by counting ink: the card's rows
  // must be bare field, and the badge's rows must be byte-identical to the state that
  // has a book -- which is what makes this one screen with its content removed rather
  // than a second screen that happens to look similar.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  reader::Framebuffer withCard(480, 800), idle(480, 800), field(480, 800);
  theme.renderSleep(withCard, ramp.fonts, reader::demoSleepVm(), reader::Plane::Bw, nullptr);
  theme.renderSleep(idle, ramp.fonts, reader::demoSleepIdleVm(), reader::Plane::Bw, nullptr);
  // The field alone, for comparison: a view model whose note is empty draws no badge.
  reader::SleepViewModel bare;
  bare.nothingToContinue = true;
  theme.renderSleep(field, ramp.fonts, bare, reader::Plane::Bw, nullptr);

  // Rows the two renders disagree on are exactly the card's rows.
  int firstDiff = -1, lastDiff = -1;
  for (int y = 0; y < 800; ++y) {
    bool diff = false;
    for (int x = 0; x < 480; ++x)
      if (withCard.getPixel(x, y) != idle.getPixel(x, y)) { diff = true; break; }
    if (diff) {
      if (firstDiff < 0) firstDiff = y;
      lastDiff = y;
    }
  }
  REQUIRE(firstDiff > 0);
  // The card is centred, so the disagreement is in the middle band and nowhere near
  // the badge at the bottom.
  CHECK(firstDiff > 100);
  CHECK(lastDiff < 700);

  // And in that band the idle render is the untouched field.
  for (int y = firstDiff; y <= lastDiff; ++y)
    for (int x = 0; x < 480; ++x)
      if (idle.getPixel(x, y) != field.getPixel(x, y)) {
        CHECK_MESSAGE(false, "the idle render is not bare field at row " << y);
        y = lastDiff;
        break;
      }
}

TEST_CASE("the idle view model says nothing about a book") {
  // Every other field on this view model describes one, so the shape of the state is
  // that they are all empty -- not that the theme is trusted to ignore them.
  const reader::SleepViewModel vm = reader::demoSleepIdleVm();
  CHECK(vm.nothingToContinue);
  CHECK(vm.title.empty());
  CHECK(vm.author.empty());
  CHECK(vm.label.empty());
  CHECK(vm.progress.empty());
  CHECK(vm.progressPercent == 0);
  CHECK_FALSE(vm.note.empty());  // ...except the one that does not
}
