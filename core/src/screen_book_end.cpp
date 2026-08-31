#include "reader/screen_book_end.h"

#include <string>

#include "reader/text.h"  // upperLatin1
#include "reader/theme.h"

namespace reader {

namespace {

// THE MIDDLE DOT, AND THE ADJACENT LITERALS ARE LOAD-BEARING. A C++ hex escape is
// UNBOUNDED, so "\xC2\xB7CH" parses `\xB7C` as ONE escape -- clang rejects it and the
// ESP32's GCC ACCEPTS it, emitting a byte that is not U+00B7. This project has already
// paid for that exact shape once. Splitting the literal ends the escape.
const char* const kMiddot = "\xC2\xB7";

}  // namespace

BookEndScreen::BookEndScreen(const Facts& facts) : FocusScreen(kRowCount, kRowCount) {
  // SHOUTED for the band, mixed for the byline -- which is what the board draws, and
  // upperLatin1 rather than upperAscii because a real card carries accented titles
  // and the device once showed `LE FLéAU`.
  vm_.bookTitle = upperLatin1(facts.bookTitle);
  vm_.title = "THE END";

  // THE SEPARATOR GOES WITH THE NAME IT SEPARATES. An EPUB is not obliged to carry
  // an author, and a byline ending in a dangling middot reads as a field that failed
  // to load rather than as a book that did not say.
  vm_.byline = facts.bookTitle;
  if (!facts.author.empty()) {
    vm_.byline += " ";
    vm_.byline += kMiddot;
    vm_.byline += " ";
    vm_.byline += facts.author;
  }

  // EMPTY WHEN UNKNOWN, never `0 CHAPTERS`. Same call homeVmForCard makes for the
  // LIBRARY row's count: "no books" and "could not look" are different claims.
  if (facts.chapterCount > 0)
    vm_.meta = std::to_string(facts.chapterCount) + " CHAPTERS";

  vm_.finishLabel = "MARK AS FINISHED";
  vm_.leaveLabel = facts.libraryBeneath ? "BACK TO LIBRARY" : "BACK TO HOME";
  vm_.note = "MARKING AS FINISHED CLEARS IT FROM \xE2\x80\x9C"
             "NOW READING\xE2\x80\x9D ON HOME.";

  // The board's own labels. BACK rather than CLOSE, because what Back does here is
  // leave a screen rather than dismiss a panel -- this is not an overlay. No holds,
  // so no slot shows a ring; and no auto-repeat, because two slabs cannot need one.
  vm_.hints = {"BACK", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);

  // The base's focus starts on the first row, which is kFinish -- the board's own
  // starting selection. The mirror below is all the focus state there is.
  syncVm();
}

void BookEndScreen::syncVm() { vm_.focusedAction = focus(); }

Action BookEndScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1, g.held);
    case Gesture::Prev:
      return moveFocus(-1, g.held);
    case Gesture::Back:
      // BACK TO THE LAST PAGE, which is why this screen is pushed rather than
      // swapped in. A reader who wanted to re-read the ending can.
      return Action::pop();
    case Gesture::Activate:
      switch (vm_.focusedAction) {
        case kFinish:
          // LATCHED, NOT DONE HERE. The write is the card's and the card is the
          // shell's; see Action::finish(). The shell leaves the book afterwards.
          return Action::finish();
        case kLeave:
          // STOPS AT THE ROOT when no Library is on the stack, so this always leaves
          // the book -- landing on the Library when one is beneath and on Home when
          // the reader arrived through CONTINUE. The LABEL is what varies; see
          // BookEndViewModel::leaveLabel.
          return Action::popTo(ScreenId::Library);
        default:
          return Action::none();
      }
    default:
      return Action::none();
  }
}

void BookEndScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                           Plane plane) const {
  theme.renderBookEnd(fb, fonts, vm_, plane);
}

}  // namespace reader
