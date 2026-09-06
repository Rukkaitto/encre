#include "reader/screen_book_error.h"

#include <utility>

#include "reader/theme.h"

namespace reader {

BookErrorScreen::BookErrorScreen(Facts facts)
    : FocusScreen(kRowCount, kRowCount), facts_(std::move(facts)) {
  // The board's caption, fixed. It does NOT carry the book's name, unlike
  // DeleteConfirm's -- the name is in the prose here, and following the board is the
  // rule. U+2019 as the board spells it (&rsquo;).
  vm_.title = "CAN\xE2\x80\x99T OPEN FILE";

  // BOTH SENTENCES NAME THE FILE, in the board's own U+201C/U+201D quotes
  // (&ldquo;/&rdquo;). A dialog that does not name the thing is one people learn to
  // dismiss without reading.
  const std::string quoted = "\xE2\x80\x9C" + facts_.displayName + "\xE2\x80\x9D";
  vm_.message =
      facts_.reason == BookErrorReason::Damaged
          // design/BookError.dc.html, verbatim.
          ? quoted +
                " appears to be damaged and can\xE2\x80\x99t be opened. The file was left"
                " untouched on the card."
          // design/BookErrorUnreadable.dc.html. Makes no promise about retrying,
          // because this board has no RETRY slab -- unlike SdMissing, which does.
          : quoted + " could not be read from the SD card. The file was left untouched.";

  vm_.okLabel = "OK";
  vm_.deleteLabel = "DELETE FILE\xE2\x80\xA6";  // U+2026, the board's &hellip;
  vm_.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // The base's focus starts on the first row, which is kOk -- the board's filled
  // slab, and where a prompt with a destructive second option belongs: a press made
  // before the user has read anything dismisses. DeleteConfirmScreen's rule.
  syncVm();
}

void BookErrorScreen::syncVm() { vm_.focusedAction = focus(); }

Action BookErrorScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Back:
      // Back IS close, which is what the board's first hint slot says -- and it is
      // the same thing OK does, so a reader who presses either gets the same result.
      return Action::pop();
    case Gesture::Activate:
      if (vm_.focusedAction == kOk) return Action::pop();
      // The confirmation owns the deleting. This screen owns saying what went wrong.
      // It is reachable on BOTH copy shapes: making it inert on `Unreadable` was
      // considered and rejected, because the shapes differ only by a sentence and a
      // reader meeting an inert slab would have nothing to learn the rule from --
      // the `works only sometimes` trap. If the card really is gone the removal
      // simply fails, which needs no branch here: FileSystem::remove reports the END
      // STATE and the list the reader lands on already says which it was.
      return Action::push(ScreenId::DeleteConfirm);
    default:
      return Action::none();
  }
}

void BookErrorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderBookError(fb, fonts, vm_, plane);
}

}  // namespace reader
