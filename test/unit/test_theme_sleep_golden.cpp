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

// TWO AUTHOR SPECIMENS, because the run now has two outcomes and a fixture that
// only reaches one of them tests half the mechanism.
//
// "Robert Louis Stevenson" is the WRAPPING case: 393px against a 312px column, so
// it takes two lines and both are COMPLETE -- which is the outcome the cap was
// chosen to buy, and 58 of the corpus's 68 overflowing names share it. It clears
// its wrap boundary by 88px (the test at the bottom of this file measures it).
reader::SleepViewModel longAuthorSleep() {
  reader::SleepViewModel vm;
  vm.label = "NOW READING";
  vm.title = "Kidnapped";
  vm.author = "Robert Louis Stevenson";
  vm.progressPercent = 22;
  vm.progress = "22% \xC2\xB7 CH. 04";
  vm.note = "ASLEEP \xC2\xB7 HOLD POWER TO WAKE";
  return vm;
}

// ...and this is the CLAMPING case, which is the name the defect was reported
// with. "Fyodor Mikhailovich Dostoevsky" is 540px and wraps to THREE lines in this
// face -- FYODOR / MIKHAILOVICH / DOSTOEVSKY -- so the cap elides the last one and
// the ellipsis appears on a run that has already been given every line it may have.
// Five distinct names in the 225-book corpus reach this, three of them corporate.
//
// It is also the exact string that rendered as `ODOR MIKHAILOVICH DOSTOEVS` before
// this change: centred at a NEGATIVE offset, painted over both card borders and out
// onto the dither field, and clipped by the panel edge.
reader::SleepViewModel clampedAuthorSleep() {
  reader::SleepViewModel vm = longAuthorSleep();
  vm.title = "Crime and Punishment";
  vm.author = "Fyodor Mikhailovich Dostoevsky";
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

// The badge's top edge, taken from a render that has NO CARD -- the idle state,
// which is this same screen with its content removed, so the badge is the same box
// in the same place. The field's own dots are one pixel wide, so the badge's border
// is the first long run on the frame.
//
// IT MUST NOT BE MEASURED RELATIVE TO THE CARD, and the first version of this file
// did exactly that: "the topmost inked row below the card's bottom". The badge is
// drawn FIRST now, so a card that overruns fills white across it -- and that search
// then finds the REMAINS of a badge the card has already destroyed, its surviving
// bottom border, and reports it as a badge the card stopped short of. Proved by
// mutation: reserving the badge once instead of twice overruns it by 35 rows, and
// the relative form passed all 24 assertions.
int badgeTopOf(const reader::Framebuffer& fb) {
  for (int y = 0; y < fb.height(); ++y) {
    int run = 0, best = 0;
    for (int x = 0; x < fb.width(); ++x) {
      if (!fb.getPixel(x, y)) {
        ++run;
        if (run > best) best = run;
      } else {
        run = 0;
      }
    }
    if (best > 100) return y;
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

  // The badge alone, which is the same box in the same place: design/SleepIdle.dc.html
  // is this screen with its content removed, drawn by the same tail.
  reader::SleepViewModel idle;
  idle.nothingToContinue = true;
  idle.note = vm.note;

  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::Framebuffer bare(w, h), fb(w, h);
    theme.renderSleep(bare, ramp.fonts, idle, reader::Plane::Bw, nullptr);
    theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);

    const int badge = badgeTopOf(bare);
    REQUIRE(badge < h);  // the badge really is down there to collide with

    const Box b = cardBox(fb, 400);
    // On the glass at all, both borders included.
    CHECK(b.top > 0);
    CHECK(b.bottom < h - 1);
    // ...and clear of the badge, which is the bound that actually binds.
    CHECK(b.bottom < badge);

    // THE BADGE IS INTACT, which is the assertion a mutant cannot slip past.
    // "The card stopped above where the badge starts" can also be satisfied by a
    // card that PAINTED OVER the badge and left only its bottom border behind,
    // because the badge is drawn first. This says the badge's whole band is
    // byte-identical to the render that has no card in it at all.
    CHECK(golden::rowsIdentical(fb, bare, badge, h));
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
  // THAT IT WRAPS AT ALL, and no more than that: pinning the exact line count here
  // would pre-empt the clearance loop below, which is the thing this case exists to
  // run. Proved by mutation -- putting the rejected specimen back made this REQUIRE
  // fire on "4 == 3" and the clearance was never measured. The count is pinned where
  // it belongs, by the card's growth above.
  REQUIRE(p.lineCount() >= 2);

  // A BREAK HAS TWO DIRECTIONS AND BOTH ARE CHECKED, which the first version of this
  // case got wrong -- it measured one and passed the very specimen it was written to
  // reject. A line sits on the wrap boundary if a small change to the face could make
  // it either GAIN its next word or LOSE its last one:
  //
  //   gain -- the next word only just failed to fit, so a slightly NARROWER measure
  //           pulls it up. That is the direction CLAUDE.md's rule names and the one
  //           test_book_error_copy.cpp measures: by how much did the next word
  //           overflow. Slack is the wrong metric for it, because a line with 15px
  //           left over is safe when the next word is 130px wide and on a knife edge
  //           when it is 14px.
  //   lose -- the line itself only just fitted, so a slightly WIDER measure pushes
  //           its last word down. Here slack IS the metric, and it is the direction
  //           the rejected specimen failed on: "JEKYLL AND MR" measures 312 in a
  //           312px column -- it fits by ZERO pixels -- while its next word
  //           overflowed by more than 100, so a next-word check alone called it safe.
  for (int i = 0; i < p.lineCount(); ++i) {
    const std::string_view line = p.lines[static_cast<size_t>(i)];
    const int slack = contentW - title.measure(line, p.tracking);
    CHECK_MESSAGE(slack >= 12, "line " << i << " (\"" << std::string(line) << "\") fits by "
                                       << slack
                                       << "px: a wider face would push its last word down");
    if (i + 1 >= p.lineCount()) continue;
    const std::string_view next = p.lines[static_cast<size_t>(i + 1)];
    // EVERY BREAK HERE FALLS AT A SPACE, which is the premise of the measurement
    // below: `WordBreak::Anywhere` may split a word, and a split has no "next word"
    // to weigh. It is also the property the specimen is chosen for -- an ordinary
    // word wrap is what a reader meets, and the mid-word break is the fallback.
    const size_t sp = next.find(' ');
    const std::string word(next.substr(0, sp == std::string_view::npos ? next.size() : sp));
    const std::string with = std::string(line) + " " + word;
    const int over = title.measure(with, p.tracking) - contentW;
    CHECK_MESSAGE(over >= 12, "line " << i << " (\"" << std::string(line) << "\") is within "
                                      << over
                                      << "px of taking its next word: a narrower face would "
                                         "pull that word up");
  }
}


// --- The author run -----------------------------------------------------------

TEST_CASE("QuietTheme renders a wrapping Sleep author to golden at both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, longAuthorSleep(), reader::Plane::Bw, nullptr);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "sleep_long_author"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "sleep_long_author_x3"); }
}

TEST_CASE("A LONG AUTHOR STAYS INSIDE THE CARD, which is the whole defect") {
  // THE TEST THIS BUG NEEDED, and nothing in the suite could have failed for it.
  //
  // The author was drawn by drawCentredText, which places a run at
  // `centreIn(0, contentW, w)` -- and centreIn returns a NEGATIVE half when the run
  // is wider than the box. So an over-wide name did not elide and did not wrap: it
  // started LEFT of the card's own padding, painted over both 2px borders and out
  // onto the dither field, and was clipped by the panel edge. Every golden passed,
  // because every golden's author was short.
  //
  // The invariant is the card's PADDING: the card is opaque white and the only thing
  // drawn inside it is drawn in the content column, so the 42px band between each
  // border and that column is paper by construction. Ink there means a run escaped.
  // That is checked rather than the panel edge because the panel edge is where the
  // damage ENDED -- the run had already crossed the border by then, and a test that
  // only watched the edge would pass for a name that merely ate the frame.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const reader::SleepViewModel vms[] = {longAuthorSleep(), clampedAuthorSleep()};
  for (const reader::SleepViewModel& vm : vms) {
    for (int i = 0; i < 2; ++i) {
      const int w = i == 0 ? 480 : 528;
      const int h = i == 0 ? 800 : 792;
      reader::Framebuffer fb(w, h);
      theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);

      const Box b = cardBox(fb, 400);
      const int cardX = (w - 400) / 2;  // centreIn of the card, both panels
      const int border = 2, padX = 42;
      int stray = 0;
      for (int y = b.top + border; y <= b.bottom - border; ++y) {
        for (int x = cardX + border; x < cardX + border + padX; ++x)
          if (!fb.getPixel(x, y)) ++stray;
        for (int x = cardX + 400 - border - padX; x < cardX + 400 - border; ++x)
          if (!fb.getPixel(x, y)) ++stray;
      }
      CHECK_MESSAGE(stray == 0, "author '" << vm.author << "' at " << w << "x" << h << ": "
                                           << stray << " inked pixels in the card's padding");
    }
  }
}

TEST_CASE("a long author makes the CARD taller rather than escaping it") {
  // The wrap's own geometry, asserted so a re-bless cannot quietly take it back. The
  // author's line box is the FACE's line height here, not one of this screen's
  // numbers, because the board leaves this run at `line-height: normal`.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int authorLine = ramp.fonts[reader::Role::Label400].lineHeight();
  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::Framebuffer one(w, h), two(w, h);
    theme.renderSleep(one, ramp.fonts, sampleSleep(), reader::Plane::Bw, nullptr);
    theme.renderSleep(two, ramp.fonts, longAuthorSleep(), reader::Plane::Bw, nullptr);
    const Box a = cardBox(one, 400), b = cardBox(two, 400);
    CHECK(b.height() - a.height() == authorLine);  // exactly one more author line
    CHECK(b.top < a.top);                          // ...and still centred
    CHECK(b.bottom > a.bottom);
  }
}

TEST_CASE("THE AUTHOR IS CAPPED AT TWO LINES AND THE TITLE KEEPS THE REMAINDER") {
  // The budget ORDER, which is the design decision this change actually made. Both
  // runs on this card can grow, so one is measured against a fixed rule and the other
  // against what is left over -- and it is the AUTHOR that takes the fixed rule,
  // because the title is the one fact this screen exists to state.
  //
  // Driven with BOTH runs unbreakable and far too long, which is the only state that
  // makes the order observable: if the title yielded first, the card would fill with
  // author and the title would be cut to a single line.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int authorLine = ramp.fonts[reader::Role::Label400].lineHeight();
  reader::SleepViewModel vm = sampleSleep();
  vm.title = std::string(255, 'W');
  vm.author = std::string(255, 'W');

  // THE BADGE IS MEASURED OFF A CARD-LESS RENDER, never relative to the card -- see
  // badgeTopOf, which carries the reason: the badge is drawn FIRST, so a card that
  // overran would paint white across it and a relative search would find the REMAINS
  // of the badge it had already destroyed and report a card that stopped short of it.
  // That trap is not the title's alone; a two-line author is one more line pushing at
  // the same bound, so this test has to measure the bound the same honest way.
  reader::SleepViewModel idle = sampleSleep();
  idle.nothingToContinue = true;

  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::Framebuffer bare(w, h), fb(w, h);
    theme.renderSleep(bare, ramp.fonts, idle, reader::Plane::Bw, nullptr);
    theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);
    const int badge = badgeTopOf(bare);
    REQUIRE(badge < h);  // the badge really is down there to collide with
    const Box b = cardBox(fb, 400);

    // Still on the glass and still clear of the badge -- the bound holds with TWO
    // growable runs pushing at it, which is what changed about this screen.
    CHECK(b.top > 0);
    CHECK(b.bottom < h - 1);
    CHECK(b.bottom < badge);

    // AND WITH THE CARD AT ITS BOUND THE TITLE IS WHAT PAYS, which is the order
    // stated as an observation rather than as a comment. Both runs are maximal here,
    // so there is no slack: the author still takes its two line boxes, and the extra
    // one comes OUT of the title's allocation rather than out of the card's bound.
    //
    // The first version of this test asserted the card GREW by an author line here
    // and failed at -17px, which is the arithmetic being right: 29px of author bought
    // against a 46px title line the budget then had to give back. That failure is
    // what the two halves below were split out of -- a card at its bound cannot grow,
    // so measuring growth there measures the floor in the title's division instead.
    reader::Framebuffer shortAuthor(w, h);
    reader::SleepViewModel titleOnly = vm;
    titleOnly.author = "X";
    theme.renderSleep(shortAuthor, ramp.fonts, titleOnly, reader::Plane::Bw, nullptr);
    const Box t = cardBox(shortAuthor, 400);
    CHECK(t.bottom < badge);
    CHECK_MESSAGE(b.height() <= t.height(),
                  "a two-line author grew the card past a one-line author's at the "
                  "bound: the title did not give way");
  }

  // THE CAP ITSELF, measured where there IS slack -- which is the only place it can
  // be seen. With a short title the card is nowhere near the badge, so an uncapped
  // author would simply keep growing it; that it grows by EXACTLY one extra line box
  // is what says the cap is two rather than unbounded.
  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::SleepViewModel one = sampleSleep(), many = sampleSleep();
    many.author = std::string(255, 'W');  // enough for many lines if nothing capped it
    reader::Framebuffer a(w, h), c(w, h);
    theme.renderSleep(a, ramp.fonts, one, reader::Plane::Bw, nullptr);
    theme.renderSleep(c, ramp.fonts, many, reader::Plane::Bw, nullptr);
    CHECK_MESSAGE(cardBox(c, 400).height() - cardBox(a, 400).height() == authorLine,
                  "an unbreakable 255-char author took "
                      << (cardBox(c, 400).height() - cardBox(a, 400).height())
                      << "px where the cap allows one extra line box of " << authorLine);
  }
}

TEST_CASE("the wrapping-author specimen is OFF the wrap boundary") {
  // The title's rule, applied to the run beside it -- see the title's own version of
  // this test above for why the SLACK is the wrong metric and the next word's
  // OVERFLOW is the right one.
  //
  // It matters more here than there, because this specimen's whole point is that both
  // its lines are complete: a break that moved would turn a two-line author into a
  // clamped one and the golden would report it as a rendering regression.
  ramp::Ramp ramp;
  const reader::Font& author = ramp.fonts[reader::Role::Label400];
  const int contentW = 400 - 2 * (2 + 42);
  const reader::Tracking tr = reader::trackingEm(author, 220);

  const std::string shouted = reader::upperLatin1(longAuthorSleep().author);
  reader::Prose p = reader::wrapProseLead(author, shouted, contentW,
                                          reader::pxToF26(author.lineHeight()), tr,
                                          reader::WordBreak::Anywhere);
  REQUIRE(p.lineCount() == 2);
  for (int i = 0; i + 1 < p.lineCount(); ++i) {
    const std::string_view line = p.lines[static_cast<size_t>(i)];
    const std::string_view next = p.lines[static_cast<size_t>(i + 1)];
    const size_t sp = next.find(' ');
    const std::string word(next.substr(0, sp == std::string_view::npos ? next.size() : sp));
    const int over = author.measure(std::string(line) + " " + word, p.tracking) - contentW;
    CHECK_MESSAGE(over >= 12, "line " << i << " (\"" << std::string(line) << "\") is within "
                                      << over << "px of taking its next word");
  }

  // ...and the CLAMPING specimen must stay firmly over the cap, for the mirror
  // reason: if it ever wrapped to two it would stop exercising the ellipsis at all.
  const std::string shoutedC = reader::upperLatin1(clampedAuthorSleep().author);
  reader::Prose c = reader::wrapProseLead(author, shoutedC, contentW,
                                          reader::pxToF26(author.lineHeight()), tr,
                                          reader::WordBreak::Anywhere);
  CHECK(c.lineCount() >= 3);
}
