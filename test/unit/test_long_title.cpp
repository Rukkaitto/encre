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
#include "reader/screen_home.h"
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
    for (int repeats : {1, 4, 12}) {
      std::string name;
      for (int i = 0; i < repeats; ++i) name += kLongTitle;

      reader::Framebuffer fb(width, height);
      fb.clear(true);
      theme.renderDeleteConfirm(fb, r.fonts, longDeleteConfirm(name), reader::Plane::Bw);

      // The panel's own top border is inside the canvas, and so is its bottom:
      // there is at least one row of veil above the first and below the last, so
      // neither edge is clipped. The veil is a 3px-pitch stipple, so a row of pure
      // paper does not exist -- what is asserted instead is that the top and
      // bottom rows of the frame carry no SOLID run as wide as the panel.
      auto longestRun = [&](int y) {
        int best = 0, run = 0;
        for (int x = 0; x < width; ++x) {
          if (!fb.getPixel(x, y)) { if (++run > best) best = run; } else run = 0;
        }
        return best;
      };
      const int panelW = 380;
      CHECK_MESSAGE(longestRun(0) < panelW, "panel border on row 0, repeats " << repeats);
      CHECK_MESSAGE(longestRun(1) < panelW, "panel border on row 1, repeats " << repeats);
      // The hint bar is drawn over the veil last, so the bottom rows are the bar's
      // -- what matters is that the panel did not reach past the bar's top.
      reader::Hint hints[4];
      const reader::DeleteConfirmViewModel vm = longDeleteConfirm(name);
      reader::buildHints(reader::kHintSlotMarks, vm.hints, vm.holds, hints);
      const int barTop = height - reader::hintBarHeight(r.fonts, hints);
      CHECK_MESSAGE(longestRun(barTop - 1) < panelW,
                    "panel border under the bar, repeats " << repeats);
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

}  // namespace

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
