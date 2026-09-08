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
  // design/Sleep.dc.html's own specimen, and design/Main.dc.html's: one book, one
  // chapter, named identically on both boards. The percentage is not a field any
  // more -- the theme composes it from progressPercent, which the bar reads too.
  // 190px against the 312px column, so it clears the elide by 122.
  vm.chapter = "I \xC2\xB7 Miss Brooke";
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
  CHECK(vm.chapter.empty());
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
  vm.chapter = "VII \xC2\xB7 Recognition";  // 218px, clears the elide by 94
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
  vm.chapter = "XIV \xC2\xB7 The Islet";  // 182px, clears the elide by 130
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
// --- A chapter name too long for the column ------------------------------------
//
// THE CARD NAMES THE CHAPTER NOW, WHERE IT SHOWED A SPINE POSITION (`6% - CH. 01`),
// and this is the specimen that exercises the one outcome the run has that the
// position never did: being wider than the card.
//
// `PREMIERE PARTIE : A LIRE AVANT L'ACHAT` is design/Main.dc.html's own long-chapter
// specimen -- a real label off a real French novel -- so the two screens that draw
// this string test it with the same one. 526px against the 312px content column, so
// it is comfortably over one line and comfortably under two, and no change to the
// face or the kern table could turn this fixture into a one-line one.
//
// IT IS THE WRAPPING SPECIMEN, WHERE IT USED TO BE THE ELIDING ONE. The run wrapped
// to two lines rather than cutting at one, and 526px is 1.7 lines -- so this string
// stopped exercising the ellipsis at the moment the wrap landed, exactly as the
// author's specimen would have if it had ever come in under two. kElidedChapter
// below is what carries the clamp now; that split is deliberate, and the guard in
// `the eliding chapter specimen really is clamped` is what stops either fixture
// quietly drifting into the other's job.
//
// It also carries the ACCENTED CAPITALS path (E-grave, A-grave) through this run,
// which is upperLatin1's business one layer down and which no other sleep fixture
// reaches -- and it carries it through a WRAP now, which is the case CLAUDE.md
// flags: the acute sits close to cap height, so an accented capital on a wrapped
// line is where a lead that is too tight would show first.
const char* const kLongChapter = "PREMI\xC3\x88RE PARTIE : \xC3\x80 LIRE AVANT L'ACHAT";

// A NAME THE TWO-LINE CAP STILL CUTS, because 8.15% of the corpus's labels need a
// third line and the eliding path is still reached for all of them.
//
// It is a REAL label, lifted verbatim from `sleep_chapter_probe`'s widest-per-book
// list -- a Jules Verne chapter heading, 1887px against the 312px column, so about
// six lines' worth of name into a two-line band. Two things about it are worth
// keeping rather than tidying: it is in capitals because the BOOK authored it that
// way (this run does not shout), and it stops mid-word at `SEEI` because it is 128
// bytes, which is `toc.h`'s kMaxTocLabelBytes doing its job. So this is not a
// contrived string -- it is what the device would put on the glass.
const char* const kElidedChapter =
    "CHAPTER XIV. IN WHICH PHILEAS FOGG DESCENDS THE WHOLE LENGTH OF THE BEAUTIFUL VALLEY OF "
    "THE GANGES WITHOUT EVER THINKING OF SEEI";

reader::SleepViewModel longChapterSleep() {
  reader::SleepViewModel vm = sampleSleep();
  vm.chapter = kLongChapter;
  return vm;
}

reader::SleepViewModel elidedChapterSleep() {
  reader::SleepViewModel vm = sampleSleep();
  vm.chapter = kElidedChapter;
  return vm;
}

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

// The progress bar's top row: the first row carrying a black run of EXACTLY the
// bar's 170px. The card's borders are 400 and the little rule is 44, so nothing
// else on this screen can be mistaken for it -- the same discriminator cardBox
// uses one width up, and `SLEEP'S BAR FILL IS INSIDE ITS BORDER` above finds the
// bar the same way.
//
// IT EXISTS TO MEASURE THE TITLE WITHOUT MEASURING THE CARD. The bar sits below
// the title and the author, so `barTopOf - cardBox().top` is the card's border,
// its top padding, the label, the little rule, three gaps, the AUTHOR's height
// and the TITLE's height -- every term but those two being a constant. With the
// author held still it is therefore the title's LINE COUNT, read off the frame
// and independent of how tall the card came out. That independence is the whole
// point: the card's height and the title's budget are two different quantities
// now, and a test that measured the title through the height could not tell one
// from the other.
int barTopOf(const reader::Framebuffer& fb) {
  for (int y = 0; y < fb.height(); ++y) {
    int run = 0;
    for (int x = 0; x < fb.width(); ++x) {
      if (!fb.getPixel(x, y)) {
        ++run;
        if (run == 170 && (x + 1 >= fb.width() || fb.getPixel(x + 1, y))) return y;
      } else {
        run = 0;
      }
    }
  }
  return -1;
}

// How many lines the chapter run wraps to, and how much of its two-line reserve
// that leaves unused -- the renderer's own wrap, through the renderer's own face
// and tracking, because a transcribed line count is a second copy of the wrap.
int chapterLinesOf(const ramp::Ramp& ramp, const std::string& chapter) {
  const reader::Font& f = ramp.fonts[reader::Role::Label500];
  const int contentW = 400 - 2 * (2 + 42);
  reader::Prose p =
      reader::wrapProseLead(f, chapter, contentW, reader::pxToF26(f.lineHeight()),
                            reader::trackingEm(f, 100), reader::WordBreak::Anywhere);
  std::string tail;
  reader::clampProse(f, p, 2, contentW, tail);  // kSleepChapterMaxLines
  return p.lineCount();
}

// The reserve the card does NOT spend: `kSleepChapterMaxLines` line boxes less
// what the name actually took. This is exactly the amount by which the title is
// CONSERVATIVE -- the budget is measured against the reserve and the card is tall
// by the actual -- so any assertion about room the card left unused has to add it
// back before comparing, or it is measuring the trade rather than the division.
int chapterSlackOf(const ramp::Ramp& ramp, const std::string& chapter) {
  if (chapter.empty()) return 0;
  const int line = ramp.fonts[reader::Role::Label500].lineHeight();
  return (2 - chapterLinesOf(ramp, chapter)) * line;
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

    // AND THE TITLE TAKES EVERY WHOLE LINE THE REMAINDER ALLOWS, which is what "the
    // title keeps the remainder" means and is the order's real observable at the
    // bound. `cardRoom` is the badge's footprint reserved TWICE off the badge's own
    // top -- the same derivation renderSleep makes -- so a card leaving a whole
    // title line box unused would mean the division shortchanged the hero.
    reader::Framebuffer shortAuthor(w, h);
    reader::SleepViewModel titleOnly = vm;
    titleOnly.author = "X";
    theme.renderSleep(shortAuthor, ramp.fonts, titleOnly, reader::Plane::Bw, nullptr);
    const Box t = cardBox(shortAuthor, 400);
    CHECK(t.bottom < badge);

    const int cardRoom = 2 * badge - h;
    const int titleLine = 46;  // kSleepTitleLineH, the board's `line-height: 1.1`
    // THE CHAPTER'S UNSPENT RESERVE IS ADDED BACK, and that term is the whole of
    // this assertion's restatement. The title's budget is measured against
    // kSleepChapterMaxLines and the card is tall by what the name TOOK, so a
    // one-line chapter -- which is what these two fixtures carry -- leaves the card
    // 29px shorter than the division it was budgeted by. Without this term the
    // assertion reads that 29px as room the hero was shortchanged out of and fails
    // at 70px (X4) / 62px (X3): it would be measuring the CONSERVATIVE BUDGET, which
    // is a deliberate trade with its own test and its own measured cost, rather than
    // the division this test is about.
    //
    // IT IS NOT A TOLERANCE. `chapterSlackOf` is the exact reserve the card declined
    // to spend, so what is compared is still the budget's own remainder -- and it is
    // still required to be under one whole title line.
    const int slack = chapterSlackOf(ramp, vm.chapter);
    for (const Box& box : {b, t}) {
      CHECK_MESSAGE(cardRoom - (box.height() + slack) < titleLine,
                    "the card left " << (cardRoom - (box.height() + slack))
                                     << "px of its budgeted room unused, which is a whole "
                                     << titleLine << "px title line the hero did not get");
    }

    // WHAT THIS DELIBERATELY NO LONGER ASSERTS: that a two-line author's card is no
    // TALLER than a one-line author's. That read as "at the bound the title pays",
    // and it was never that property -- it was the remainder of a division, and it
    // has now been wrong in both directions.
    //
    // The card's height is `F + A + C + 46 * floor((cardRoom - F - A - C) / 46)`,
    // which collapses to `cardRoom - (budget mod 46)`. So whether the author's
    // second line costs the title a line depends ONLY on whether the one-line
    // author's remainder is at least the 29px that line takes:
    //
    //   with a one-line chapter   remainder 24 (X4) / 16 (X3)  -> title gives a line
    //                                                             back, card -17px
    //   with a two-line chapter   remainder 41 (X4) / 33 (X3)  -> the slack absorbs
    //                                                             it, card +29px
    //
    // Both are correct renders: the card is 12px (X4) and 4px (X3) inside its bound
    // and clear of the badge, asserted directly above. The second case is the BETTER
    // one -- the title keeps all six of its lines instead of dropping to six from
    // seven -- so an assertion that fails on it is measuring the wrong thing. This
    // test's own history says so: its first version asserted the card GREW here and
    // failed at -17px, and the version that replaced it asserted the card SHRANK and
    // failed at +29px. A property that flips sign when a neighbouring run takes one
    // more line was an artefact of `mod 46` both times.
    //
    // The order itself -- author capped first, title given the remainder -- is
    // asserted where it is actually observable: by the cap costing exactly one extra
    // line box where there IS slack, in the loop below.
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

// --- The chapter run ------------------------------------------------------------

TEST_CASE("QuietTheme renders a wrapping Sleep chapter to golden at both geometries") {
  // LOOKING AT THE PIXELS IS THE POINT, which is why this is a golden and not only
  // the arithmetic below. Home shipped a use-after-free on a neighbouring run whose
  // only symptom was a column of NOTDEF BOXES -- ink that spells nothing inks rows
  // exactly like ink that spells something, so every geometric assertion passed. A
  // rendered long chapter is the check that tells those apart, and it is sharper on
  // this fixture than on any other here because the accented capitals it carries
  // are the glyphs a botched wrap would drop.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, longChapterSleep(), reader::Plane::Bw, nullptr);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "sleep_long_chapter"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "sleep_long_chapter_x3"); }
}

TEST_CASE("QuietTheme renders a CLAMPED Sleep chapter to golden at both geometries") {
  // THE ELIDING PATH IS ITS OWN RENDER AND NEEDS ITS OWN PIXELS, which the wrapping
  // golden above cannot give it: kLongChapter fits two lines whole, so it never
  // reaches clampProse at all.
  //
  // What makes this worth a golden rather than arithmetic is `chapterTail`. It is a
  // named local in renderSleep for one reason -- clampProse's elided last line is a
  // NEW string, not a view into the name -- and it exists ONLY on this path. Home
  // shipped precisely that mistake on a neighbouring run, passing a temporary
  // inline, and its symptom was a column of NOTDEF BOXES: correct for a short
  // string and wrong for one long enough to be cut, which is silently right in
  // exactly the case every other fixture covers. Ink that spells nothing inks rows
  // exactly like ink that spells something, so no geometric assertion can see it.
  // This golden is the only thing in the suite that can.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  auto renderOne = [&](int w, int h, const std::string& name) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, elidedChapterSleep(), reader::Plane::Bw, nullptr);
    golden::checkGolden(fb, name);
  };
  SUBCASE("X4 480x800") { renderOne(480, 800, "sleep_elided_chapter"); }
  SUBCASE("X3 528x792") { renderOne(528, 792, "sleep_elided_chapter_x3"); }
}

TEST_CASE("the two chapter specimens really do wrap and really do clamp") {
  // THE GUARD THE AUTHOR'S CLAMPED FIXTURE ALREADY CARRIES, for its reason: a
  // fixture that drifts under its own boundary stops testing the thing it was added
  // for and says nothing about it. This run has TWO boundaries now and a specimen
  // either side, so there are two ways to go quiet -- kLongChapter falling back to
  // one line would stop exercising the WRAP, and kElidedChapter fitting two would
  // stop exercising the CLAMP. Neither shows up as a failure anywhere else: both
  // goldens would simply be re-blessed onto a shorter render.
  //
  // A mutation tells you about your INPUT before it tells you about your test, and
  // this is the assertion that makes the input speak for itself.
  ramp::Ramp ramp;
  const reader::Font& f = ramp.fonts[reader::Role::Label500];
  const int contentW = 400 - 2 * (2 + 42);
  const reader::Tracking tr = reader::trackingEm(f, 100);  // kSleepChapterEm
  auto linesOf = [&](const std::string& s) {
    return reader::wrapProseLead(f, s, contentW, reader::pxToF26(f.lineHeight()), tr,
                                 reader::WordBreak::Anywhere)
        .lineCount();
  };
  // The board's specimen is ONE line -- which is what makes it the case that proves
  // the reservation is unconditional, since a conditional band would shrink for it.
  CHECK(linesOf(sampleSleep().chapter) == 1);
  // Wraps, and is NOT clamped: every byte of this name reaches the glass.
  CHECK(linesOf(kLongChapter) == 2);
  // Over the cap, so clampProse elides its second line.
  CHECK(linesOf(kElidedChapter) >= 3);
}

TEST_CASE("A LONG CHAPTER STAYS INSIDE THE CARD, which is the defect this run could have") {
  // THE TEST THIS CHANGE NEEDED, and it is the author's test one run lower for the
  // author's reason -- because it is literally the same latent defect.
  //
  // The run this replaced was a spine position, `CH. 01`, drawn by drawCentredText
  // and never wider than the column. drawCentredText places a run at
  // `centreIn(0, contentW, w)`, and centreIn returns a NEGATIVE half when the run is
  // wider than its box: a card-sourced chapter name handed to it unbounded would
  // begin LEFT of the card's padding, paint over both 2px borders out onto the
  // dither field, and be clipped by the panel edge with no ellipsis to say so. 34.54%
  // of the corpus's 8,617 chapter labels are wider than this column, so it would have
  // been the COMMON case rather than an edge one.
  //
  // THE INVARIANT IS THE CARD'S PADDING, AND ON THIS SCREEN IT IS THE ONLY PLACE INK
  // IS EVIDENCE AT ALL. The card is opaque white and everything drawn inside it is
  // drawn in the content column, so the 42px band between each border and that column
  // is paper by construction: ink there means a run escaped.
  //
  // Nothing else on this screen can answer the question, which is worth stating
  // because the obvious alternatives were tried against the mutation and both are
  // blind. The panel EDGE is inked on every row by the dither field, and the card's
  // own left and right BORDERS are inked on every row of the card -- so neither a
  // full-row scan nor an in-card extent can separate the run's ink from furniture
  // that is legitimately there. Measured with the elide removed: a 385px name (`VI -
  // The Flight in the Heather`, an ordinary chapter of a real novel) leaves the
  // content column and reaches neither, while a 526px one reaches the glass edge --
  // so a test watching the edge would have passed for the first and this one fails
  // for both, with 508 and 742 stray pixels.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  // Four shapes of over-wide name: two real labels with spaces -- one that the wrap
  // fits WHOLE in two lines and one the clamp has to cut -- then one unbreakable
  // token (a filename fallback, which is what WordBreak::Anywhere exists for), and
  // FAT's maximum long-name length. So nothing here depends on where a space happens
  // to be, and both sides of the two-line cap are covered: the wrap and the clamp
  // are different code paths out of this run and either could be the one that
  // escapes.
  const std::string names[] = {kLongChapter, kElidedChapter, std::string(120, 'W'),
                               std::string(255, 'M')};
  for (const std::string& name : names) {
    for (int i = 0; i < 2; ++i) {
      const int w = i == 0 ? 480 : 528;
      const int h = i == 0 ? 800 : 792;
      reader::SleepViewModel vm = sampleSleep();
      vm.chapter = name;
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
      CHECK_MESSAGE(stray == 0, "chapter of " << name.size() << " bytes at " << w << "x" << h
                                              << ": " << stray
                                              << " inked pixels in the card's padding");
    }
  }
}

TEST_CASE("the card's HEIGHT follows the chapter's actual wrap") {
  // ONE OF THE TWO ASSERTIONS THAT ARE THIS SPLIT'S WHOLE SPECIFICATION. The card
  // is tall by what the chapter name TOOK; the title's budget is measured against
  // what it MIGHT take. Two different quantities, and this is the one that has to
  // follow the name.
  //
  // IT REPLACES `the chapter costs the card TWO lines however long the name is`,
  // which asserted the opposite -- and correctly, for a card whose height reserved
  // the second line too. `min-height: 58px` came off design/Sleep.dc.html because
  // that reserve bought nothing in the height: this is the card's LAST run, so
  // nothing below it steps up, and a one-line name (65.46% of corpus labels) left
  // 29px of empty box standing at the foot of a card that holds the glass for
  // HOURS. Dead space, not spacing.
  //
  // THE FIXTURE IS THE BOARD'S OWN, AND ITS TITLE IS ONE LINE ON PURPOSE -- which
  // is what makes this test blind to the budget question and therefore able to name
  // the height one. `MIDDLEMARCH` is nowhere near its budget, so the division above
  // it cannot change what is drawn however the chapter is counted into it; the only
  // thing left that can move the card is the chapter's own wrap. The REQUIRE below
  // says so rather than leaving it to be inferred.
  //
  // Reversing this -- giving `cardH` the reserve again -- fails every assertion
  // here and none in `the TITLE's budget does NOT follow it`.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int chapterLine = ramp.fonts[reader::Role::Label500].lineHeight();
  auto cardHeight = [&](const reader::SleepViewModel& vm, int w, int h) {
    reader::Framebuffer fb(w, h);
    theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);
    return cardBox(fb, 400).height();
  };

  // A mutation tells you about your INPUT before it tells you about your test: the
  // three chapter shapes below only differ if the wrap really resolves them to one
  // line, two lines and a clamp.
  REQUIRE(chapterLinesOf(ramp, sampleSleep().chapter) == 1);
  REQUIRE(chapterLinesOf(ramp, kLongChapter) == 2);
  REQUIRE(chapterLinesOf(ramp, kElidedChapter) == 2);  // clamped TO the cap

  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;
    reader::SleepViewModel one = sampleSleep(), two = sampleSleep(), many = sampleSleep();
    two.chapter = kLongChapter;
    many.chapter = std::string(255, 'M');
    reader::SleepViewModel without = sampleSleep();
    without.chapter.clear();

    const int hOne = cardHeight(one, w, h);

    // A SECOND LINE OF NAME COSTS EXACTLY ONE LINE BOX AND NOTHING ELSE. Not zero,
    // which is what a reserved height gives, and not more, which would mean some
    // other term moved with it.
    CHECK_MESSAGE(cardHeight(two, w, h) - hOne == chapterLine,
                  "a two-line chapter made the card " << (cardHeight(two, w, h) - hOne)
                                                      << "px taller where its second line box is "
                                                      << chapterLine
                                                      << ": the height is not following the wrap");

    // ...AND A NAME OVER THE CAP COSTS THE CAP AND NO MORE, which is clampProse's
    // half of it: 255 unbreakable characters is about eight lines of name into a
    // two-line band, so a height that followed the wrap without the clamp would run
    // the card off the glass.
    CHECK_MESSAGE(cardHeight(many, w, h) == cardHeight(two, w, h),
                  "a 255-byte chapter and a two-line one differ by "
                      << (cardHeight(many, w, h) - cardHeight(two, w, h))
                      << "px: the clamp is not bounding the height");

    // AND A ONE-LINE NAME COSTS ONE LINE PLUS THE GAP, which is the assertion the
    // reserved form fails: it gave this the same 2 * chapterLine + 14 the two-line
    // name gets. kSleepGap is 14.
    CHECK_MESSAGE(hOne - cardHeight(without, w, h) == chapterLine + 14,
                  "a one-line chapter costs the card "
                      << (hOne - cardHeight(without, w, h))
                      << "px where one line box plus the gap is " << (chapterLine + 14)
                      << ": the height is reserving a line the name did not take");
  }

  // AND AN EMPTY CHAPTER COSTS NO LINE AT ALL, which is what an old pointer -- one
  // written before last.json carried a chapter -- gets. Not a blank band at the
  // foot of the card: this is the card's LAST run, so nothing below it steps up, and
  // a shorter card is the honest rendering of an absent claim. (The arithmetic is
  // the assertion directly above; this is the claim it is making, named.)
}

TEST_CASE("the TITLE's budget does NOT follow it") {
  // THE OTHER HALF OF THE SPECIFICATION, AND THE REASON THE RESERVE STILL EXISTS.
  //
  // A CHAPTER CHANGES WHILE THE BOOK IS BEING READ AND AN AUTHOR DOES NOT. The
  // title takes what the card's room leaves, so a budget that counted this run's
  // second line only when the name USED it would make the TITLE's line budget
  // depend on where the reader is standing: cross a chapter boundary and the book's
  // name reflows, or newly acquires an ellipsis, because a page was turned. That is
  // a visible defect with a baffling cause, and `chapterReserveH` is what answers
  // it -- the budget is measured against kSleepChapterMaxLines whatever the wrap
  // does.
  //
  // IT IS MEASURED WITHOUT MEASURING THE CARD, which is the whole difficulty: the
  // card's height is SUPPOSED to move here -- by one chapter line box, which is what
  // `the card's HEIGHT follows the chapter's actual wrap` asserts -- so any
  // observable derived from cardBox().height() cannot separate "the title reflowed"
  // from "the card followed the chapter". `barTopOf` reads the title's line count
  // off the frame instead: the bar's distance below the card's own TOP is every
  // fixed term of the card plus the author's height plus the title's, and the author
  // is held still. So this assertion is blind to the height question and fails only
  // on the budget one.
  //
  // THE FIXTURE HAD TO BE CHOSEN AND NOT PICKED, AND THE FIRST ONE DID NOT BITE.
  // Two conditions have to hold together for a budget change to be VISIBLE, and a
  // maximal title alone gives only the first:
  //
  //   1. the title must FILL its budget, or one more line of budget changes nothing
  //      that is drawn -- so 255 unbreakable characters, which wants ~45 lines;
  //   2. the budget's own REMAINDER must be at least `titleLine - chapterLine`, or
  //      the chapter's unspent 29px does not carry the floor over to another line.
  //
  // The first version of this test copied `THE AUTHOR IS CAPPED AT TWO LINES`'
  // fixture, which maximises the author too -- and a two-line author leaves a
  // remainder of 12px (X4) / 4px (X3), so 29px more budget still floored to the same
  // 6 lines and the mutation passed all 30 assertions. A mutation tells you about
  // your INPUT before it tells you about your test. With the board's own one-line
  // author the remainder is 41px (X4) / 33px (X3) and the crossing happens, so the
  // author here is deliberately SHORT while the title is deliberately maximal.
  //
  // BOTH CONDITIONS ARE ASSERTED RATHER THAN TRUSTED, off the frame and not from
  // transcribed arithmetic: `cardRoom - height` is the budget's remainder, the same
  // quantity `THE AUTHOR IS CAPPED AT TWO LINES` compares against a title line. If
  // a future ramp or board change moves it out of [17, 46) this test goes quiet, and
  // the REQUIREs are what make it say so instead.
  //
  // AND THEY ARE MEASURED ON THE TWO-LINE CHAPTER, WHICH IS THE ONLY PLACE THEY CAN
  // BE. A fixture guard has to be blind to the defect it is guarding a test for.
  // With a two-line name the reserve is exactly what the name takes, so both
  // spellings of the budget give the same number and both give the same card -- the
  // guards read 41px (X4) / 33px (X3) whichever expression `maxTitleLines` holds.
  // Measured on the ONE-line render they do not: the mutation grows the card by a
  // title line there, the remainder goes to -5px, and condition 2 fires with
  // `this fixture cannot see the defect` about a fixture that can see it perfectly
  // well. A guard that accuses the fixture when the code is wrong is worse than no
  // guard, and that is how the first version of this read.
  //
  // Reversing this -- giving `maxTitleLines` the ACTUAL -- fails here and nowhere
  // else in this file.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int titleLine = 46;  // kSleepTitleLineH
  const int chapterLine = ramp.fonts[reader::Role::Label500].lineHeight();
  reader::SleepViewModel base = sampleSleep();
  base.title = std::string(255, 'W');  // fills any budget
  // ...and the author stays the board's one-line `George Eliot`, on purpose. See above.

  auto render = [&](const std::string& chapter, reader::Framebuffer& fb) {
    reader::SleepViewModel vm = base;
    vm.chapter = chapter;
    theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);
  };

  // The badge off a CARD-LESS render, never relative to the card -- badgeTopOf
  // carries the reason, and cardRoom is the badge's footprint reserved twice.
  reader::SleepViewModel idle = sampleSleep();
  idle.nothingToContinue = true;

  for (int i = 0; i < 2; ++i) {
    const int w = i == 0 ? 480 : 528;
    const int h = i == 0 ? 800 : 792;

    reader::Framebuffer bare(w, h);
    theme.renderSleep(bare, ramp.fonts, idle, reader::Plane::Bw, nullptr);
    const int cardRoom = 2 * badgeTopOf(bare) - h;

    auto band = [&](const std::string& chapter) {
      reader::Framebuffer fb(w, h);
      render(chapter, fb);
      const int bar = barTopOf(fb);
      REQUIRE(bar > 0);
      return bar - cardBox(fb, 400).top;
    };

    // The two guards, on the two-line render where the reserve is exact -- see above
    // for why they may not be taken from the one-line one.
    reader::Framebuffer probe(w, h);
    render(kLongChapter, probe);
    REQUIRE(chapterSlackOf(ramp, kLongChapter) == 0);  // the reserve really is exact here
    const int remainder = cardRoom - cardBox(probe, 400).height();

    // CONDITION 1: the title really is filling its budget, so the division is
    // observable at all. The card leaves less than a whole title line unused.
    REQUIRE_MESSAGE(remainder < titleLine,
                    "the title is not filling its budget: " << remainder
                                                            << "px of budgeted room unused");
    // CONDITION 2: and the chapter's unspent reserve would carry the floor to one
    // more title line if the budget were allowed to see it. Without this the
    // mutation this test exists for is invisible -- which is exactly what happened
    // to its first fixture.
    REQUIRE_MESSAGE(remainder >= titleLine - chapterLine,
                    "the budget's remainder is only "
                        << remainder << "px, so a chapter's unspent " << chapterLine
                        << "px could not buy a " << titleLine
                        << "px title line: this fixture cannot see the defect");

    const int oneLine = band(sampleSleep().chapter);
    for (const std::string& chapter :
         {std::string(kLongChapter), std::string(kElidedChapter)}) {
      CHECK_MESSAGE(band(chapter) == oneLine,
                    "the title moved the bar by " << (band(chapter) - oneLine)
                                                  << "px between a one-line chapter and this one ("
                                                  << titleLine
                                                  << "px is a whole title line): the budget is "
                                                  << "following the chapter's wrap");
    }
  }
}

TEST_CASE("the figure under the bar is the number the BAR reads, not a second field") {
  // It was a string on the view model -- `6% - CH. 01`, composed by the shell -- so
  // the figure under the bar and the length of the bar were two spellings of one
  // fact, and nothing stopped them disagreeing. The theme composes it from
  // progressPercent now, which is the field drawProgressBar already takes.
  //
  // Asserted by MOVING the percentage and watching the run's ink change: a render
  // that had kept a separate string would draw the same glyphs at 6% and at 87%.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  auto inkOfRun = [&](int percent) {
    reader::SleepViewModel vm = sampleSleep();
    vm.progressPercent = percent;
    // The chapter is cleared so the band measured below holds the percentage alone.
    vm.chapter.clear();
    reader::Framebuffer fb(480, 800);
    theme.renderSleep(fb, ramp.fonts, vm, reader::Plane::Bw, nullptr);
    const Box b = cardBox(fb, 400);
    // The percentage is the card's last run with the chapter cleared, so its line box
    // is the one above the bottom padding: the border is 2 and kSleepCardPadY is 38,
    // and BOTH have to come off or the band lands in the padding and inks nothing.
    // (It did, first time round, and the REQUIRE below is what said so rather than
    // the CHECK quietly comparing two zeroes.)
    const int line = ramp.fonts[reader::Role::Label500].lineHeight();
    const int runBottom = b.bottom - 2 - 38;
    int n = 0;
    for (int y = runBottom - line; y < runBottom; ++y)
      for (int x = (480 - 400) / 2 + 2 + 42; x < (480 - 400) / 2 + 400 - 2 - 42; ++x)
        if (!fb.getPixel(x, y)) ++n;
    return n;
  };
  const int at6 = inkOfRun(6);
  const int at87 = inkOfRun(87);
  REQUIRE(at6 > 0);  // the run really is where this looked, or the case proves nothing
  CHECK_MESSAGE(at6 != at87, "`6%` and `87%` inked the same "
                                 << at6 << " pixels: the run is not reading progressPercent");
}
