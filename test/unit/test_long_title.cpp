// A title longer than the box the board drew for it.
//
// Every board's sample title is a word ("Middlemarch", "Dubliners") and every
// golden in the suite is blessed against one, so nothing in the suite exercised
// the case the user actually hit: a real card, whose filenames are as long as
// whoever made them felt like. This file is that case, on each of the four
// screens that draw a title, and it is here as its own file rather than folded
// into the per-screen tests because what it pins is one behaviour crossing several
// screens -- the boards' `text-overflow: ellipsis`, and the two deliberate
// exceptions to it.
//
// AND IT IS SIX SCREENS NOW. BookEnd's byline is the sixth, and it arrived by the
// same route as Home's: the run was drawn with a one-line primitive, a real card's
// title ran off both margins, and nothing here noticed because every board's sample
// title is a word.
//
// IT SAID "FOUR SCREENS" AND COVERED FOUR THAT DO NOT INCLUDE HOME, which draws the
// most prominent title on the device. Home elided, the device showed a truncated
// book name on the one screen whose whole job is to name the book being read, and
// nothing here noticed -- so changing Home's title to wrap broke no test. Home's
// cases are below.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
#include "reader/icons.h"
#include "reader/screens.h"
#include "reader/screen_book_end.h"
#include "reader/screen_book_error.h"
#include "reader/screen_home.h"
#include "reader/text.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

using ramp::Ramp;

namespace {

// Long, and long in the way a card is long: no spaces anywhere, because the
// filenames people actually have are underscore- or hyphen-joined and CSS offers
// no break opportunity at either. A name with spaces in it would wrap and would
// quietly not test the hard case.
const char* const kLongTitle =
    "Middlemarch_A_Study_of_Provincial_Life_George_Eliot_1871_unabridged";
const char* const kLongMeta = "GEORGE_ELIOT_TRANSLATED_ANNOTATED_AND_INTRODUCED_BY_SOMEBODY";

reader::LibraryViewModel longLibrary() {
  reader::LibraryViewModel vm;
  // The band's label is data too -- a subfolder's own name -- so the screen it
  // draws is a folder, not the root.
  vm.title = reader::upperLatin1(kLongTitle);
  vm.bookCount = 12;
  vm.rows.push_back({"Classics_and_Other_Assorted_Older_Books", "FOLDER \xC2\xB7 6 BOOKS", "",
                     true});
  vm.rows.push_back({kLongTitle, kLongMeta, "6%", false});
  vm.rows.push_back({"Jane Eyre", "CHARLOTTE BRONT\xC3\x8B", "DONE", false});
  vm.rows.push_back({kLongTitle, kLongMeta, "31%", false});
  vm.focusedRow = 1;
  vm.hints = {"BACK", "OPEN", "UP", "DOWN"};
  vm.holds = {false, true, false, false};
  return vm;
}

reader::BookDetailsViewModel longBookDetails() {
  reader::BookDetailsViewModel vm;
  vm.title = kLongTitle;
  vm.author = "George Eliot";
  vm.format = "EPUB";
  vm.fields = {{"Progress", "6% \xC2\xB7 PAGE 53 OF 890"},
               {"Current story", "MISS BROOKE"},
               {"Bookmarks", "0"},
               {"File size", "1.8 MB"},
               {"Location", "/BOOKS/"}};
  vm.hints = {"BACK", "", "", ""};
  return vm;
}

reader::ItemActionsViewModel longItemActions(std::string title) {
  reader::ItemActionsViewModel vm;
  vm.title = std::move(title);
  vm.status = "31%";
  vm.actions = {{"Open", true}, {"Book details", true}, {"Mark as finished", false},
                {"Delete\xE2\x80\xA6", false}};
  vm.focusedAction = 0;
  vm.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  return vm;
}

reader::DeleteConfirmViewModel longDeleteConfirm(const std::string& name) {
  reader::DeleteConfirmViewModel vm;
  // Composed the way DeleteConfirmScreen composes it, quotes and all.
  vm.title = "DELETE \xE2\x80\x9C" + reader::upperLatin1(name) + "\xE2\x80\x9D?";
  vm.message =
      "The file leaves the SD card. Your progress and bookmarks are kept in case it comes back.";
  vm.cancelLabel = "CANCEL";
  vm.confirmLabel = "DELETE";
  vm.hints = {"CANCEL", "SELECT", "UP", "DOWN"};
  return vm;
}

// THE NAMES A PANEL THAT QUOTES A FILENAME HAS TO SURVIVE. 67 characters is a real
// card's name; 255 is FAT's LONG-FILENAME MAXIMUM, so it is the longest a card can
// hold and is where a bound has to hold rather than happen to; 804 is well past
// what FAT allows, because a bound that only just holds is one that will stop
// holding. No spaces anywhere, for kLongTitle's reason.
std::vector<std::string> pathologicalNames() {
  std::string longest(kLongTitle);
  while (longest.size() < 255) longest += kLongTitle;
  longest.resize(255);
  std::string huge;
  for (int i = 0; i < 12; ++i) huge += kLongTitle;
  return {std::string(kLongTitle), longest, huge};
}

// The topmost inked row at or below `from`, or -1.
int firstInkedRow(const reader::Framebuffer& fb, int from) {
  for (int y = from; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x)
      if (!fb.getPixel(x, y)) return y;
  return -1;
}

}  // namespace

TEST_CASE("a long title on the Library truncates and matches its golden") {
  Ramp r;
  reader::QuietTheme theme;
  reader::Framebuffer fb(480, 800);
  const reader::LibraryViewModel vm = longLibrary();
  theme.renderLibrary(fb, r.fonts, vm, reader::Plane::Bw);
  // One geometry, deliberately: what varies with the panel is the column's width,
  // and that is pinned at BOTH widths by the ink-bounds checks in
  // test_components.cpp. What this golden pins is the composition -- four runs on
  // one row all cut to one edge, an ellipsis on each, the band's label cut too.
  golden::checkGolden(fb, "library_long_title");
}

TEST_CASE("a long title on Book details wraps and matches its golden") {
  Ramp r;
  reader::QuietTheme theme;
  reader::Framebuffer fb(480, 800);
  const reader::BookDetailsViewModel vm = longBookDetails();
  theme.renderBookDetails(fb, r.fonts, vm, reader::Plane::Bw);
  golden::checkGolden(fb, "book_details_long_title");
}

TEST_CASE("Book details keeps every field row above the hint bar, however long the title") {
  Ramp r;
  reader::QuietTheme theme;
  // The wrap is what makes this screen's height a result rather than a constant,
  // and spec 4.1b says the screen is a fixed single-page summary -- so the thing
  // that must not happen is a title tall enough to push the last field row under
  // the hint bar. Both geometries, because the line budget is derived from the
  // canvas (235px of block room on the X4 against 227 on the X3) and the two
  // therefore take different branches of the same arithmetic.
  struct Case {
    int w, h;
  };
  for (const Case c : {Case{480, 800}, Case{528, 792}}) {
    for (int repeats = 1; repeats <= 8; ++repeats) {
      reader::BookDetailsViewModel vm = longBookDetails();
      vm.title.clear();
      for (int i = 0; i < repeats; ++i) vm.title += kLongTitle;

      reader::Framebuffer fb(c.w, c.h);
      theme.renderBookDetails(fb, r.fonts, vm, reader::Plane::Bw);

      reader::Hint hints[4];
      reader::buildHints(reader::kHintSlotMarks, vm.hints, vm.holds, hints);
      const int barTop = c.h - reader::hintBarHeight(r.fonts, hints);

      // The bar's own top rule is the first ink at or below barTop. Anything
      // inked in the band ABOVE it that belongs to a field row would mean the
      // rows had been pushed down into the bar; the check is therefore that the
      // first ink from barTop onwards is exactly the rule, i.e. the row of pixels
      // immediately above it is clear of row ink.
      CHECK_MESSAGE(firstInkedRow(fb, barTop) == barTop,
                    "repeats " << repeats << " at " << c.w << "x" << c.h);
      // And the last field row's rule-less bottom edge is above the bar: scan for
      // the last fully blank row before the bar, which must exist -- the board's
      // `margin-top: auto` slack.
      bool blankRowBeforeBar = false;
      for (int y = barTop - 1; y >= barTop - 4 && y > 0; --y) {
        bool blank = true;
        for (int x = 0; x < c.w; ++x)
          if (!fb.getPixel(x, y)) { blank = false; break; }
        if (blank) { blankRowBeforeBar = true; break; }
      }
      CHECK_MESSAGE(blankRowBeforeBar, "no slack above the bar at repeats " << repeats
                                                                           << " on " << c.w);
    }
  }
}

TEST_CASE("the actions panel is the same height for a long title as for a short one") {
  Ramp r;
  reader::QuietTheme theme;
  // The board truncates this caption on one line, and the reason is structural
  // rather than cosmetic: the panel is centred, its height is the sum of what is
  // in it, and ItemActionsScreen's partial-repaint footprint is keyed on that
  // height. A caption that wrapped would make every book a different panel.
  for (int width : {480, 528}) {
    reader::Framebuffer shortFb(width, 800);
    shortFb.clear(true);
    theme.renderItemActions(shortFb, r.fonts, longItemActions("Dubliners"), reader::Plane::Bw);

    reader::Framebuffer longFb(width, 800);
    longFb.clear(true);
    theme.renderItemActions(longFb, r.fonts, longItemActions(kLongTitle), reader::Plane::Bw);

    // The panel's top border is the first full-width-ish run of ink inside the
    // veil; comparing the two frames' first and last inked rows is enough, since
    // a taller panel would move both.
    CHECK(firstInkedRow(shortFb, 0) == firstInkedRow(longFb, 0));
    // Rows are identical outside the caption, which is what "same box" means:
    // every row from the first action row down to the panel's bottom border must
    // match byte for byte.
    const int captionBottom =
        firstInkedRow(shortFb, 0) + reader::kPanelBorder + 2 * reader::kPanelCaptionPadY +
        r.fonts[reader::Role::Label500].lineHeight() + reader::kPanelCaptionRuleH;
    int differing = 0;
    for (int y = captionBottom; y < 800; ++y)
      for (int x = 0; x < width; ++x)
        if (shortFb.getPixel(x, y) != longFb.getPixel(x, y)) ++differing;
    CHECK(differing == 0);
  }
}

TEST_CASE("the delete panel stays on the glass for a pathologically long name") {
  Ramp r;
  reader::QuietTheme theme;
  // This caption is the one that keeps WRAPPING (it is a sentence, and an ellipsis
  // would eat its question mark), so its height is what grows -- and the panel is
  // centred, so a panel taller than the canvas would be cut off at both ends. FAT
  // allows a 255-character name; the clamp is what bounds it.
  for (int width : {480, 528}) {
    const int height = width == 480 ? 800 : 792;
    for (const std::string& name : pathologicalNames()) {
      const size_t chars = name.size();  // reported by the messages below

      const reader::DeleteConfirmViewModel vm = longDeleteConfirm(name);
      reader::Framebuffer fb(width, height);
      fb.clear(true);
      theme.renderDeleteConfirm(fb, r.fonts, vm, reader::Plane::Bw);

      // ASSERTED FROM THE PANEL'S OWN BORDER COLUMNS, which is the corrupt-book
      // dialog's form and replaces the one this case used to carry. That one asked
      // whether rows 0, 1 and barTop-1 hold a SOLID run as wide as the panel, and it
      // has two holes: a panel that overflows a LOT puts its border off the canvas
      // entirely, so row 0 holds only the two 2px side segments and the check passes;
      // and the CAPTION'S RULE spans the content width between those same segments,
      // so a `>= panelW` run does not tell a border from a rule either.
      //
      // The side borders are inked on EVERY row the panel occupies and nothing else
      // on this frame inks them -- the veil is white-on-paper, the caption's rule and
      // the slabs are inset, and the parent is not drawn -- so the first and last row
      // carrying both is the panel's true extent.
      const int panelW = 380;
      const int panelX = (width - panelW) / 2;
      reader::Hint hints[4];
      reader::buildHints(reader::kHintSlotMarks, vm.hints, vm.holds, hints);
      const int barTop = height - reader::hintBarHeight(r.fonts, hints);
      auto borderRow = [&](int y) {
        return !fb.getPixel(panelX, y) && !fb.getPixel(panelX + panelW - 1, y);
      };
      int top = -1, bottom = -1;
      for (int y = 0; y < barTop; ++y)
        if (borderRow(y)) {
          if (top < 0) top = y;
          bottom = y;
        }
      CHECK_MESSAGE(top > 0, "no top border inside the canvas, name of " << chars << " chars at "
                                                                        << width);
      CHECK_MESSAGE(bottom > top, "no bottom border inside the canvas, name of "
                                      << chars << " chars at " << width);
      // Strictly above the bar: the scan stops at barTop, so a panel that runs into
      // the bar reports bottom == barTop - 1 rather than where it really ends.
      CHECK_MESSAGE(bottom < barTop - 1,
                    "panel reaches the hint bar, name of " << chars << " chars at " << width);
    }
  }
}


// --- Home ---------------------------------------------------------------------

namespace {

// Home's four hint marks, which decide the bar's height and therefore where the
// menu starts. `kHomeMarks` is file-local to theme_quiet.cpp and stays that way --
// widening a theme's surface for a test is the wrong trade -- so this is the same
// four icons, and it is the one place a drift between them would show up as a
// failure here rather than as a wrong answer.
const reader::Icon* const kHomeHintMarks[4] = {&reader::icons::kBook, &reader::icons::kDot,
                                               &reader::icons::kUp, &reader::icons::kDown};

reader::HomeViewModel longHome() {
  reader::HomeViewModel vm = reader::demoHomeVm();
  vm.title = kLongTitle;
  return vm;
}

// A REAL CHAPTER NAME, and the reason this file gained a case that is not a title:
// Home's chapter line is a second run whose words come off the CARD, and it is drawn
// with a one-line primitive -- which is character for character how Home's title, and
// then BookEnd's byline, got here. This one is quoted in CLAUDE.md as the long-name
// case for the Reader's own header band, so it is a publisher's string and not an
// invention.
const char* const kLongChapter = "PREMI\xC3\x88RE PARTIE : \xC3\x80 LIRE AVANT L'ACHAT";

reader::HomeViewModel homeWithChapter(const std::string& chapter) {
  reader::HomeViewModel vm = reader::demoHomeVm();
  vm.chapterLabel = chapter;
  return vm;
}

// The rows and columns in which two Home renders differ. Used instead of the theme's
// own geometry because `rightX` is file-local to theme_quiet.cpp: diffing against a
// render with an EMPTY chapter label isolates that one run without this file holding
// a second copy of the cover width and the gutter.
struct Band {
  int firstRow = -1, lastRow = -1, firstCol = -1, lastCol = -1;
  bool any() const { return firstRow >= 0; }
  int rows() const { return lastRow - firstRow + 1; }
  int inkWidth() const { return lastCol - firstCol + 1; }
};

Band diffBand(const reader::Framebuffer& a, const reader::Framebuffer& b) {
  Band d;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.getPixel(x, y) == b.getPixel(x, y)) continue;
      if (d.firstRow < 0) d.firstRow = y;
      d.lastRow = y;
      if (d.firstCol < 0 || x < d.firstCol) d.firstCol = x;
      if (x > d.lastCol) d.lastCol = x;
    }
  }
  return d;
}

}  // namespace

// --- Home's chapter line ------------------------------------------------------
//
// It held `CH. 14 OF 36` -- a spine position of a spine count -- which was a false
// claim reported off an X3, and it holds the chapter's NAME now. That is a run of
// arbitrary length arriving from a card, on a screen whose every board specimen is
// short, which is exactly the shape this file exists for.

TEST_CASE("a long chapter name on Home elides inside its column") {
  Ramp r;
  reader::QuietTheme theme;
  const reader::Font& meta = r.fonts[reader::Role::Meta400];
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::Framebuffer blank(w, h), lng(w, h);
    theme.renderHome(blank, r.fonts, homeWithChapter(""), reader::Plane::Bw);
    theme.renderHome(lng, r.fonts, homeWithChapter(kLongChapter), reader::Plane::Bw);

    // THE NAME REALLY DOES OVERFLOW at this geometry, or the case below proves
    // nothing about eliding -- a mutation that fails nothing tells you about your
    // input first.
    const int natural = meta.measure(kLongChapter, reader::trackingEm(meta, reader::kTightMetaEm));
    REQUIRE(natural > w - 2 * reader::kMargin);

    const Band d = diffBand(blank, lng);
    REQUIRE(d.any());
    // THE RUN'S OWN INK STOPS INSIDE THE MARGIN. Asserted on the DIFF's extent rather
    // than on the whole frame's right margin, which is not paper: the header band's
    // rule is full-bleed. What the unelided drawText did was carry this run to the
    // panel edge, and the diff is exactly this run.
    CHECK_MESSAGE(d.lastCol < w - reader::kMargin,
                  "chapter name reaches " << d.lastCol << " of " << w << ", margin at "
                                          << w - reader::kMargin);
    // ONE LINE BOX, not two: this run may not wrap. The title above it grows into a
    // budget derived from everything below the block, and a second growable run in
    // that column would have to be told which of the two yields.
    //
    // IT ALSO PINS THE BLANK STATE, which is why the comparison is against an EMPTY
    // label rather than against the demo one. An empty chapter is legal -- a pointer
    // written before last.json carried the key cannot say -- and it must still cost
    // its line: skipping the `ry` advance for it would make every run BELOW this one
    // differ between the two frames, and the band would run to the bottom of the block
    // instead of holding to one line.
    CHECK(d.rows() <= meta.lineHeight());
  }
}

TEST_CASE("a long chapter name on Home matches its golden") {
  // THE CHECK THE CASE ABOVE CANNOT MAKE. It proves the run stops inside its column,
  // and a column of notdef boxes stops inside a column too -- Home's title shipped
  // exactly that, a `Prose` over a temporary that had already died, and every
  // row-counting assertion passed because a notdef box inks rows like a letter does.
  // A golden is what distinguishes ink that spells something from ink that does not,
  // and it is also what pins the ellipsis being there at all.
  Ramp r;
  reader::QuietTheme theme;
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, r.fonts, homeWithChapter(kLongChapter), reader::Plane::Bw);
    golden::checkGolden(fb, w == 480 ? "home_long_chapter" : "home_long_chapter_x3");
  }
}

TEST_CASE("Home's chapter name is drawn at the board's 0.10em, not the counter's 0.16em") {
  // THE TRACKING AND THE ELIDE ARE ONE FACT, which is why this is here rather than in
  // a typography test: elideToWidth decides the cut by MEASURING, and text.h says the
  // tracking handed to it "must be the same value the run will be drawn with" -- a
  // measurement taken at different spacing is a different answer, and the failure mode
  // is the overhang the elide exists to remove.
  //
  // 0.10em is design/Main.dc.html's own number for a chapter name: it is what this
  // board gave the chapter line it drew before `CH. 01 OF 24` displaced it, and
  // `kMetaEm`'s 0.16em belonged to the counter.
  // COMPARED AGAINST THE RUN DRAWN BOTH WAYS rather than against a slack around a
  // measured advance: an ink extent is the advance less the trailing tracking and two
  // side bearings, so a tolerance wide enough to absorb those is wide enough to absorb
  // part of the 0.06em under test. drawTextElided short-circuits when the text fits,
  // so the reference here is exactly what a correct Home draws.
  Ramp r;
  reader::QuietTheme theme;
  const reader::Font& meta = r.fonts[reader::Role::Meta400];
  const std::string label = "I \xC2\xB7 Miss Brooke";
  auto inkWidthOf = [&](int em1000) {
    reader::Framebuffer fb(480, 800);
    fb.clear();
    reader::drawText(fb, meta, 0, meta.lineHeight(), label, reader::Ink::Black,
                     reader::trackingEm(meta, em1000), reader::Plane::Bw);
    int first = -1, last = -1;
    for (int x = 0; x < 480; ++x) {
      for (int y = 0; y < 800; ++y) {
        if (fb.getPixel(x, y)) continue;
        if (first < 0) first = x;
        last = x;
        break;
      }
    }
    REQUIRE(first >= 0);
    return last - first + 1;
  };
  const int tightInk = inkWidthOf(reader::kTightMetaEm);
  const int wideInk = inkWidthOf(reader::kMetaEm);
  // The two candidate trackings have to be far enough apart to tell apart at all, or
  // the checks below pass whichever one the theme used.
  REQUIRE(wideInk - tightInk > 12);
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::Framebuffer blank(w, h), named(w, h);
    theme.renderHome(blank, r.fonts, homeWithChapter(""), reader::Plane::Bw);
    theme.renderHome(named, r.fonts, homeWithChapter(label), reader::Plane::Bw);
    const Band d = diffBand(blank, named);
    REQUIRE(d.any());
    // The board's specimen fits both columns, so it is drawn whole -- and drawn whole
    // it is exactly as wide as the same run at the same tracking, to the pixel.
    CHECK(d.inkWidth() == tightInk);
    CHECK(d.inkWidth() != wideInk);
  }
}

TEST_CASE("a long title on Home matches its golden") {
  // THE ONLY CHECK HERE THAT WOULD HAVE CAUGHT THE BUG THIS CODE SHIPPED WITH.
  // `Prose::lines` are views into the text handed to the wrap, and the theme passed
  // a temporary -- so a title long enough to wrap drew from freed memory and came
  // out as a column of notdef boxes. Every row-counting assertion below passed
  // anyway, because a notdef box inks rows exactly like a letter does.
  //
  // A golden is what distinguishes ink that spells something from ink that does not,
  // and it is what the Library and Book details cases in this file already use.
  Ramp r;
  reader::QuietTheme theme;
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, r.fonts, longHome(), reader::Plane::Bw);
    golden::checkGolden(fb, w == 480 ? "home_long_title" : "home_long_title_x3");
  }
}

TEST_CASE("a long title on Home WRAPS rather than eliding") {
  Ramp r;
  reader::QuietTheme theme;
  reader::Framebuffer plain(480, 800), wrapped(480, 800);
  theme.renderHome(plain, r.fonts, reader::demoHomeVm(), reader::Plane::Bw);
  theme.renderHome(wrapped, r.fonts, longHome(), reader::Plane::Bw);
  // The whole point: a long name takes more vertical room than a short one. If it
  // elided, these two frames would ink the same rows.
  int plainRows = 0, wrappedRows = 0;
  for (int y = 0; y < 800; ++y) {
    for (int x = 0; x < 480; ++x) {
      if (!plain.getPixel(x, y)) { ++plainRows; break; }
    }
    for (int x = 0; x < 480; ++x) {
      if (!wrapped.getPixel(x, y)) { ++wrappedRows; break; }
    }
  }
  CHECK(wrappedRows > plainRows);
}

TEST_CASE("a short title on Home is bit-identical to the drawText that elided it") {
  // The wrap replaced a drawTextElided, and drawProse's first baseline is
  // baselineIn's own definition -- so an ordinary one-line title must move NOTHING.
  // This is what let the Home goldens keep passing through the change, and it is
  // asserted rather than inferred from them.
  Ramp r;
  reader::QuietTheme theme;
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, r.fonts, reader::demoHomeVm(), reader::Plane::Bw);
    golden::checkGolden(fb, w == 480 ? "home_quiet" : "home_quiet_x3");
  }
}

TEST_CASE("Home keeps its menu and hint bar exactly where they were, however long the title") {
  Ramp r;
  reader::QuietTheme theme;
  // The menu and the bar are BOTTOM-ANCHORED, and the title's line budget is derived
  // from the room left over once they and the slab are accounted for. So the thing
  // that must not happen is a name tall enough to push any of them -- which is the
  // same property Book details asserts about its field rows, on a screen whose
  // fixed furniture is different.
  //
  // Both geometries, because the budget is arithmetic over the canvas and the two
  // take different branches of it.
  struct Case {
    int w, h;
  };
  for (const Case c : {Case{480, 800}, Case{528, 792}}) {
    reader::Framebuffer shortFb(c.w, c.h);
    theme.renderHome(shortFb, r.fonts, reader::demoHomeVm(), reader::Plane::Bw);

    reader::Hint hints[4];
    const reader::HomeViewModel probe = reader::demoHomeVm();
    reader::buildHints(kHomeHintMarks, probe.hints, probe.holds, hints);
    const int menuTop = c.h - reader::hintBarHeight(r.fonts, hints) -
                        static_cast<int>(probe.menu.size()) * 81;

    for (int repeats = 1; repeats <= 8; ++repeats) {
      reader::HomeViewModel vm = longHome();
      vm.title.clear();
      for (int i = 0; i < repeats; ++i) vm.title += kLongTitle;

      reader::Framebuffer fb(c.w, c.h);
      theme.renderHome(fb, r.fonts, vm, reader::Plane::Bw);

      // EVERY ROW FROM THE MENU DOWN IS BYTE-IDENTICAL to the short-title render.
      // Stronger than "nothing fell off the bottom": it says the title cannot move
      // the furniture at all, however many lines the budget grants it.
      for (int y = menuTop; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
          if (fb.getPixel(x, y) != shortFb.getPixel(x, y)) {
            CHECK_MESSAGE(false, "row " << y << " moved at repeats " << repeats << " on " << c.w
                                        << "x" << c.h);
            y = c.h;  // one failure per case is enough
            break;
          }
        }
      }
    }
  }
}

TEST_CASE("a pathologically long Home title stays inside the canvas") {
  Ramp r;
  reader::QuietTheme theme;
  std::string huge;
  for (int i = 0; i < 40; ++i) huge += kLongTitle;
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::HomeViewModel vm = longHome();
    vm.title = huge;
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, r.fonts, vm, reader::Plane::Bw);
    // No crash, and the frame is still a frame: the bottom row belongs to the hint
    // bar's content, not to a title that ran off the end.
    reader::Hint hints[4];
    reader::buildHints(kHomeHintMarks, vm.hints, vm.holds, hints);
    CHECK(reader::hintBarHeight(r.fonts, hints) > 0);
  }
}

TEST_CASE("an accented title shouts, and its accented capitals have real glyphs") {
  // THE CASE THE DEVICE REPORTED: `Le Fleau` with an acute came out `LE FLeAU`,
  // because the shout was ASCII-only. test_text.cpp pins the mapping; this pins the
  // RENDER, which is the half a string comparison cannot see -- an uppercase accent
  // the font subset lacked would map correctly and then draw as a notdef box.
  //
  // fontc.py's subset is 0x20..0x7E plus ALL of Latin-1, so the glyphs are there; this
  // is what says so.
  Ramp r;
  reader::QuietTheme theme;
  reader::HomeViewModel vm = reader::demoHomeVm();
  vm.title = "Le Fl\xC3\xA9""au";
  vm.author = "Stephen King";
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::Framebuffer fb(w, h);
    theme.renderHome(fb, r.fonts, vm, reader::Plane::Bw);
    golden::checkGolden(fb, w == 480 ? "home_accented_title" : "home_accented_title_x3");
  }
}


// --- The end of a book ------------------------------------------------------------

TEST_CASE("a long title on BookEnd matches its golden") {
  // THE ONLY CHECK THAT DISTINGUISHES INK THAT SPELLS SOMETHING FROM INK THAT DOES
  // NOT, which on a WRAPPED run is the whole hazard: `Prose::lines` are views into the
  // text handed to the wrap, so a temporary would draw a wrapped title as a column of
  // notdef boxes and render a short one correctly -- and a notdef box inks rows
  // exactly like a letter does, so every structural assertion in
  // test_theme_book_end.cpp passes either way. Home shipped that bug; this is the
  // check that would have caught it.
  //
  // Rendered through the screen rather than the theme, because the BYLINE is composed
  // by the screen -- the title, the separator and the author are one string it owns,
  // and it is that string the wrap holds views into.
  Ramp r;
  reader::QuietTheme theme;
  reader::BookEndScreen::Facts f;
  f.bookTitle = kLongTitle;
  f.author = "George Eliot";
  f.chapterCount = 24;
  f.libraryBeneath = true;
  for (const int w : {480, 528}) {
    const int h = w == 480 ? 800 : 792;
    reader::BookEndScreen s(f);
    reader::Framebuffer fb(w, h);
    s.render(fb, r.fonts, theme, reader::Plane::Bw);
    golden::checkGolden(fb, w == 480 ? "book_end_long_title" : "book_end_long_title_x3");
  }
}

TEST_CASE("the corrupt-book dialog keeps a real card's name inside its panel") {
  // THE SAME DEFECT AS THE DELETE PANEL'S, ONE RUN DOWN. That board puts the
  // filename in the CAPTION, which wraps `Anywhere` and is clamped; this one puts it
  // in the PARAGRAPH, which had neither -- so a name with no space in it wrapped to a
  // single line WIDER than the column, was drawn through the panel's right border and
  // off the glass, and the rest of the name was lost. Found by rendering it and
  // looking, which is what every board's one-word sample name prevents.
  Ramp r;
  reader::QuietTheme theme;
  // ALL THREE SHAPES, because the bound is not the same arithmetic on each of them.
  // The OutOfMemory shape draws no `DELETE FILE...` slab, so its fixed height is 80px
  // smaller (one kActionH and the gap that separated the two slabs) -- which means its
  // paragraph is allowed 80px MORE room before it is clamped, and a bound that holds
  // for the two-slab shapes says nothing about the one that budgets differently.
  const reader::BookErrorReason kReasons[] = {reader::BookErrorReason::Damaged,
                                              reader::BookErrorReason::Unreadable,
                                              reader::BookErrorReason::OutOfMemory};
  for (int width : {480, 528}) {
    const int height = width == 480 ? 800 : 792;
    for (const reader::BookErrorReason reason : kReasons)
    for (const std::string& name : pathologicalNames()) {
      const size_t chars = name.size();  // reported by the messages below
      const int why = static_cast<int>(reason);  // reported alongside, so a failure names the shape
      reader::BookErrorScreen s({"/books/" + name, name, reason, reader::ScreenId::Library});

      reader::Framebuffer fb(width, height);
      fb.clear(true);
      theme.renderBookError(fb, r.fonts, s.vm(), reader::Plane::Bw);

      const int panelW = 380;
      const int panelX = (width - panelW) / 2;
      // HORIZONTAL: no ink outside the panel's own columns, above the hint bar --
      // which is full width by design and is drawn last, so it is excluded by row.
      reader::Hint hints[4];
      reader::buildHints(reader::kHintSlotMarks, s.vm().hints, s.vm().holds, hints);
      const int barTop = height - reader::hintBarHeight(r.fonts, hints);
      int outside = 0;
      for (int y = 0; y < barTop; ++y)
        for (int x = 0; x < width; ++x)
          if ((x < panelX || x >= panelX + panelW) && !fb.getPixel(x, y)) ++outside;
      // The veil is white-on-paper, so it inks nothing here: any black outside the
      // panel is a glyph that escaped it.
      CHECK_MESSAGE(outside == 0, "ink outside the panel, name of "
                                       << chars << " chars at " << width
                                       << " reason " << why);

      // VERTICAL: the same defect turned ninety degrees. The panel is centred, so one
      // taller than the canvas is cut off at BOTH ends.
      //
      // ASSERTED FROM THE BORDER COLUMNS, and the two forms that came before it both
      // had holes. "The frame's first and last rows carry no panel-wide run" is the
      // delete panel's own form: a panel that overflows a LITTLE puts a border on row
      // 0 and it bites, a panel that overflows a LOT puts that border off the canvas
      // and it passes. Finding the first and last row with a `>= panelW` run does not
      // fix it either, and this case shipped believing it did: the CAPTION'S RULE
      // spans the content width (376) between the two 2px side borders, so the three
      // are contiguous and that row measures 380 as well. With a 255-character name
      // it read top=8 bottom=82 barTop=736 -- top and bottom BOTH landing on the
      // caption's rule, with the real bottom border 55px under the hint bar.
      //
      // The panel's two side borders are inked on EVERY row it occupies and nothing
      // else on this frame inks them: the veil is white-on-paper, the caption's rule
      // and the slabs are inset, and the parent is not drawn. So the first and last
      // row carrying both is the panel's true extent, whatever is between them.
      auto borderRow = [&](int y) {
        return !fb.getPixel(panelX, y) && !fb.getPixel(panelX + panelW - 1, y);
      };
      int top = -1, bottom = -1;
      for (int y = 0; y < barTop; ++y)
        if (borderRow(y)) {
          if (top < 0) top = y;
          bottom = y;
        }
      CHECK_MESSAGE(top > 0, "no top border inside the canvas, name of "
                                 << chars << " chars at " << width << " reason " << why);
      CHECK_MESSAGE(bottom > top, "no bottom border inside the canvas, name of "
                                      << chars << " chars at " << width
                                      << " reason " << why);
      // Strictly above the bar: the scan stops at barTop, so a panel that runs into
      // the bar reports bottom == barTop - 1 rather than where it really ends.
      CHECK_MESSAGE(bottom < barTop - 1,
                    "panel reaches the hint bar, name of "
                        << chars << " chars at " << width << " reason " << why);
    }
  }
}
