// Sleep had NO golden, and that is how its progress bar stayed wrong.
//
// The screen is implemented in the theme and reachable in the simulator, but
// nothing pinned its pixels -- so when the bar was drawn over its own border at
// full height instead of inset inside it, as design/Sleep.dc.html declares with
// `box-sizing: border-box`, every test in the suite still passed. Home's four
// goldens caught the same defect on Home in six pixels. This is the pair Sleep
// was missing.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/components.h"
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
  vm.note = "ASLEEP \xC2\xB7 HOLD POWER TO WAKE";
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

// --- A title too long for one line ---------------------------------------------
//
// design/Sleep.dc.html's title WRAPS, and it used to elide (issue #74). This screen
// holds the glass for HOURS, so a name cut short is not a truncation the reader
// presses past -- it is the one they live with, on the screen whose whole job is to
// say which book they are reading.
//
// IT NEEDS HOME'S GOLDEN FOR HOME'S REASON. Home shipped a USE-AFTER-FREE on this
// exact mechanism: `Prose::lines` are string_views into the text handed to the wrap,
// the shouted string was passed inline as a temporary, and a title long enough to
// WRAP drew from freed memory as a column of notdef boxes -- while a short one
// rendered correctly, because the freed bytes were still there. Every golden passed,
// because a notdef box inks rows exactly like a letter does and the demo title is one
// line. What caught it was rendering a long title and LOOKING at it, so that is what
// `sleep_long_title` is: the check that distinguishes ink that spells something from
// ink that does not. The tests below cannot make that judgement and do not pretend
// to; they pin the geometry the wrap has to obey.

namespace {

// Three lines at both geometries, against the card's 312px content column at ppem
// 42 where MIDDLEMARCH's eleven characters measure 294. A real book's name rather
// than a synthetic run, and with spaces in it, so what is pinned is the ORDINARY
// word wrap and `WordBreak::Anywhere` is only the fallback -- which is the way round
// a reader meets them.
//
// AND IT IS OFF THE WRAP BOUNDARY BY DESIGN, which the first choice was not. "The
// Strange Case of Dr Jekyll and Mr Hyde" wraps its third line to "JEKYLL AND MR" at
// exactly 312px against a 312px column: it fits by ZERO pixels, so any change to the
// face, the ramp or the kern table would move a line and the golden would report a
// wrap change as a rendering regression. This project has a rule about it (a
// specimen must not put a line on the wrap boundary) and the test below is what
// makes the rule mechanical for this fixture rather than a thing to remember.
reader::SleepViewModel longTitleSleep() {
  reader::SleepViewModel vm;
  vm.label = "NOW READING";
  vm.title = "Far from the Madding Crowd";
  vm.author = "Thomas Hardy";
  vm.progressPercent = 41;
  vm.progress = "41% \xC2\xB7 CH. 07";
  vm.note = "ASLEEP \xC2\xB7 HOLD POWER TO WAKE";
  return vm;
}

struct Box {
  int top = -1, bottom = -1;
  int height() const { return bottom - top + 1; }
};

// The card's border box, found by the rows carrying a CONTIGUOUS black run of
// exactly the card's width -- its own top and bottom borders, two rows each.
//
// That discriminator is the one test_book_error_copy.cpp had to learn. "The first
// and last row with a wide black run" resolves to a rule INSIDE the panel the moment
// the real border leaves the canvas, which is how the delete panel's off-glass bound
// passed a test written to catch it. Here the trap is sharper still: the dither field
// covers the WHOLE panel, so every row of this screen has black pixels at both
// margins and any leftmost-to-rightmost measure returns the panel's width.
//
// The little rule (44px) and the progress bar (170px) are too narrow to be mistaken
// for a border, and the badge is WIDER than the card on both panels -- so this asks
// for exactly four rows and treats any other answer as a failure rather than
// quietly returning the wrong box.
Box cardBox(const reader::Framebuffer& fb, int cardW) {
  std::vector<int> borders;
  for (int y = 0; y < fb.height(); ++y) {
    int run = 0, best = 0;
    for (int x = 0; x < fb.width(); ++x) {
      if (!fb.getPixel(x, y)) {  // getPixel reports WHITE, so black is its negation
        ++run;
        if (run > best) best = run;
      } else {
        run = 0;
      }
    }
    if (best == cardW) borders.push_back(y);
  }
  REQUIRE_MESSAGE(borders.size() == 4,
                  "expected two 2px card borders, found " << borders.size() << " rows");
  Box b;
  b.top = borders.front();
  b.bottom = borders.back();
  return b;
}

// The topmost inked row below the card, which is the badge's top edge -- and the
// bound the card may not reach. Taken from the frame rather than from drawBadge's
// constants, which are private to components.cpp; renderSleep asks drawBadge itself.
int badgeTopBelow(const reader::Framebuffer& fb, int below, int cardW) {
  for (int y = below; y < fb.height(); ++y) {
    int run = 0, best = 0;
    for (int x = 0; x < fb.width(); ++x) {
      if (!fb.getPixel(x, y)) {
        ++run;
        if (run > best) best = run;
      } else {
        run = 0;
      }
    }
    // The field's own dots are one pixel wide; the badge's border is a long run.
    if (best > cardW / 4) return y;
  }
  return fb.height();
}

}  // namespace

TEST_CASE("QuietTheme renders a wrapping Sleep title to golden at both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, longTitleSleep(), reader::Plane::Bw, nullptr);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "sleep_long_title"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "sleep_long_title_x3"); }
}

TEST_CASE("a long Sleep title makes the CARD taller rather than losing its end") {
  // The behaviour the issue asked for, asserted on geometry so a re-bless cannot
  // quietly take it back. The card's height is a RESULT, so a title needing four line
  // boxes buys three more of them and the card grows by exactly that -- 46px a line,
  // design/Sleep.dc.html's `line-height: 1.1` on `--t-title`, and no other term.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::Framebuffer one(w, h), many(w, h);
    theme.renderSleep(one, ramp.fonts, sampleSleep(), reader::Plane::Bw, nullptr);
    theme.renderSleep(many, ramp.fonts, longTitleSleep(), reader::Plane::Bw, nullptr);
    const Box a = cardBox(one, 400), b = cardBox(many, 400);
    const int grew = b.height() - a.height();
    CHECK(grew == 2 * 46);  // three lines against one
    // ...and it is still CENTRED, which is what makes the growth symmetric.
    CHECK(b.top < a.top);
    CHECK(b.bottom > a.bottom);
  }
}

TEST_CASE("THE CARD NEVER REACHES THE BADGE, and the reserve is taken TWICE") {
  // The defect renderDeleteConfirm and renderBookError each shipped once: a CENTRED
  // panel budgeted against the whole canvas is sliced by whatever is anchored to the
  // bottom, because centreIn splits the slack EVENLY -- so reserving the badge once
  // still leaves a tall card hanging half a badge into it.
  //
  // Here the consequence is worse than a sliced border. The badge is opaque white, so
  // a title that ran under it would be HIDDEN by it: an ellipsis by another name, on
  // the screen this wrap exists to keep honest.
  //
  // Driven at FAT's long-name maximum as one unbreakable word, which is the title a
  // real card can hold once a book has no metadata and the name falls back to its
  // filename. Nothing shorter reaches the bound.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  reader::SleepViewModel vm = sampleSleep();
  vm.title = std::string(255, 'W');  // the widest cap in the face, 255 of them

  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);
    const Box b = cardBox(fb, 400);
    // On the glass at all, both borders included.
    CHECK(b.top > 0);
    CHECK(b.bottom < h - 1);
    // And clear of the badge, which is the bound that actually binds.
    const int badge = badgeTopBelow(fb, b.bottom + 1, 400);
    CHECK(badge < h);         // the badge really is down there to collide with
    CHECK(b.bottom < badge);  // ...and the card stopped short of it
  }
}

TEST_CASE("the wrapped Sleep title survives the heap moving under it") {
  // A BELT TO sleep_long_title's BRACES, and stated as exactly that: this cannot see
  // a notdef box, and only a person looking at the golden can. What it can see is the
  // lifetime bug that PRODUCES one -- a Prose whose string_views outlived their text
  // reads freed memory, and freed memory that has since been reused reads differently.
  //
  // So the same screen is rendered twice with a large allocation scribbled between
  // the two, and the frames must be byte-identical. It is a one-sided check: passing
  // does not prove the lifetimes are right, but a failure is unambiguous, and it
  // cannot flake, because a correct render is deterministic.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  reader::Framebuffer first(480, 800), second(480, 800);
  theme.renderSleep(first, ramp.fonts, longTitleSleep(), reader::Plane::Bw, nullptr);
  {
    std::vector<char> churn(1 << 20, '\xA5');
    CHECK(churn.front() == '\xA5');
  }
  theme.renderSleep(second, ramp.fonts, longTitleSleep(), reader::Plane::Bw, nullptr);
  bool same = true;
  for (int y = 0; y < 800 && same; ++y)
    for (int x = 0; x < 480; ++x)
      if (first.getPixel(x, y) != second.getPixel(x, y)) {
        CHECK_MESSAGE(false, "the render moved with the heap at (" << x << ", " << y << ")");
        same = false;
        break;
      }
}

TEST_CASE("the wrapping-title specimen is OFF the wrap boundary") {
  // CLAUDE.md's rule, made mechanical for this fixture the way
  // test_book_error_copy.cpp made it mechanical for the corrupt-book copy: a
  // specimen line that fits its column by a pixel or two agrees with Chrome by luck,
  // and flips on any change to the face, the ramp or the kern table -- at which point
  // a golden reports a wrap change as a rendering regression and the real defect is
  // in the fixture.
  //
  // THE SLACK IS THE WRONG METRIC AND IS NOT WHAT THIS MEASURES. A line with 15px
  // left over is perfectly safe when the next word is 130px wide and on a knife edge
  // when the next word is 14px; what decides a break is by how much the NEXT WORD
  // overflowed. So this measures the overflow, and asks for at least 12px of it --
  // ~3% of the column, which is about how much wider the firmware's whole-pixel
  // advances measure than Chrome's subpixel ones, plus a little.
  //
  // The first choice of specimen FAILED this at 0px: "The Strange Case of Dr Jekyll
  // and Mr Hyde" puts "JEKYLL AND MR" at exactly 312 in a 312px column.
  ramp::Ramp ramp;
  const reader::Font& title = ramp.fonts[reader::Role::Title700];
  const int contentW = 400 - 2 * (2 + 42);  // the card's own content column, both panels

  const std::string shouted = reader::upperLatin1(longTitleSleep().title);
  reader::Prose p = reader::wrapProseLead(title, shouted, contentW, reader::pxToF26(46),
                                          reader::Tracking{}, reader::WordBreak::Anywhere);
  REQUIRE(p.lineCount() == 3);

  for (int i = 0; i + 1 < p.lineCount(); ++i) {
    const std::string_view line = p.lines[static_cast<size_t>(i)];
    const std::string_view next = p.lines[static_cast<size_t>(i + 1)];
    // EVERY BREAK HERE FALLS AT A SPACE, which is the premise of the measurement
    // below: `WordBreak::Anywhere` may split a word, and a split has no "next word"
    // to weigh. It is also the property the specimen is chosen for -- an ordinary
    // word wrap is what a reader meets, and the mid-word break is the fallback.
    const size_t sp = next.find(' ');
    const std::string word(next.substr(0, sp == std::string_view::npos ? next.size() : sp));
    const std::string with = std::string(line) + " " + word;
    const int over = title.measure(with, p.tracking) - contentW;
    CHECK_MESSAGE(over >= 12, "line " << i << " (\"" << std::string(line)
                                      << "\") is within " << over
                                      << "px of taking its next word: too close to the "
                                         "wrap boundary for a specimen");
  }
}
