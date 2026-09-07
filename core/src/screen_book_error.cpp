#include "reader/screen_book_error.h"

#include <cstring>
#include <utility>

#include "reader/book.h"
#include "reader/theme.h"

namespace reader {

BookErrorReason bookErrorReasonFor(const char* why) {
  if (why == nullptr) return BookErrorReason::Damaged;
  // THE PREFIX FIRST, because it is the class of refusal that is not about the file
  // and the other two both are. See book.h for why the class is a prefix and not a
  // code, and test_heapguard.cpp for what stops that being a convention on trust.
  if (std::strncmp(why, kOpenOutOfMemory, std::strlen(kOpenOutOfMemory)) == 0)
    return BookErrorReason::OutOfMemory;
  if (std::strcmp(why, kOpenCannotOpen) == 0) return BookErrorReason::Unreadable;
  return BookErrorReason::Damaged;
}

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
  switch (facts_.reason) {
    case BookErrorReason::Damaged:
      // design/BookError.dc.html, verbatim.
      vm_.message = quoted +
                    " appears damaged and can\xE2\x80\x99t be opened. The file was left"
                    " untouched on the card.";
      break;
    case BookErrorReason::Unreadable:
      // design/BookErrorUnreadable.dc.html. Makes no promise about retrying,
      // because this board has no RETRY slab -- unlike SdMissing, which does.
      vm_.message = quoted + " could not be read from the card. The file was left untouched.";
      break;
    case BookErrorReason::OutOfMemory:
      // design/BookErrorMemory.dc.html. It says WHAT and not WHAT TO DO, and the
      // omission is deliberate: the reader has no way to free memory on purpose --
      // there is no second book to close and no restart control -- and the one thing
      // that reliably helps, a power cycle, is a promise about the resume path that
      // this screen is in no position to make. Naming the transience (`right now`)
      // is as far as the firmware actually knows.
      //
      // The second sentence is `Damaged`'s, character for character, because it is
      // the same fact and one rule should have one spelling.
      vm_.message = quoted +
                    " needs more memory than is free right now. The file was left"
                    " untouched on the card.";
      break;
  }

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
      // REPLACE, NOT PUSH. Both screens are overlays and App::render draws every
      // overlay above the topmost non-overlay, so a push left THIS panel standing
      // under the confirmation's veil -- and unlike the actions panel, which the
      // confirmation covers completely, this one's paragraph makes it taller, so it
      // stood out above and below. Reported off the device.
      return Action::replace(ScreenId::DeleteConfirm);
    default:
      return Action::none();
  }
}

void BookErrorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderBookError(fb, fonts, vm_, plane);
}

}  // namespace reader
