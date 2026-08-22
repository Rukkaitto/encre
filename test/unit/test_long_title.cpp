// A title longer than the box the board drew for it.
//
// Every board's sample title is a word ("Middlemarch", "Dubliners") and every
// golden in the suite is blessed against one, so nothing in the suite exercised
// the case the user actually hit: a real card, whose filenames are as long as
// whoever made them felt like. This file is that case, on each of the four
// screens that draw a title, and it is here as its own file rather than folded
// into the per-screen tests because what it pins is one behaviour crossing four
// screens -- the boards' `text-overflow: ellipsis`, and Book details' deliberate
// exception to it.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/framebuffer.h"
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
  vm.title = reader::upperAscii(kLongTitle);
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
  vm.subtitle = "Eight books \xC2\xB7 1871";
  vm.format = "EPUB";
  vm.fields = {{"Progress", "6% \xC2\xB7 PAGE 53 OF 890"},
               {"Current story", "MISS BROOKE"},
               {"Bookmarks", "0"},
               {"File size", "1.8 MB"},
               {"Added", "AUG 14, 2026"},
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
  vm.title = "DELETE \xE2\x80\x9C" + reader::upperAscii(name) + "\xE2\x80\x9D?";
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
