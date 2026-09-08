#include "reader/screen_book_error.h"

#include <array>
#include <cstring>
#include <string>
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

int BookErrorScreen::rowsFor(BookErrorReason reason) {
  return reason == BookErrorReason::OutOfMemory ? 1 : kRowCount;
}

// `facts` IS READ BEFORE IT IS MOVED, which is safe and not a coincidence: a base
// class is initialised before any member, so `facts.reason` here runs strictly
// before `facts_(std::move(facts))` below.
BookErrorScreen::BookErrorScreen(Facts facts)
    : FocusScreen(rowsFor(facts.reason), rowsFor(facts.reason)),
      facts_(std::move(facts)) {
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
  // THE SECOND SLAB IS GONE ON THE OutOfMemory SHAPE, and it is REMOVED rather than
  // made inert -- design/BookErrorMemory.dc.html carries the reasoning and
  // HomeEmpty's cut action slab is the precedent. The file is fine; offering to
  // delete a good book to fix a transient shortage is a nudge in the wrong
  // direction, and a reader might take it. An inert slab would be the `works only
  // sometimes` trap instead, which is the recorded reason the slab is live on
  // `Unreadable` -- there the shapes differ only by a sentence, so a reader meeting
  // a dead slab would have nothing to learn the rule from. Nothing to press is
  // nothing to learn.
  vm_.offersDelete = facts_.reason != BookErrorReason::OutOfMemory;
  // U+2026, the board's &hellip;. Cleared with the flag because there is no box left
  // to draw it in -- but the FLAG is what the renderer asks; see viewmodel.h for why
  // this is not `deleteLabel.empty()`.
  vm_.deleteLabel = vm_.offersDelete ? "DELETE FILE\xE2\x80\xA6" : "";
  // THE BAR FOLLOWS THE PANEL. With one slab there is nothing to choose between, so
  // `SELECT` would promise a choice that does not exist and Up/Down would promise a
  // second row -- the hint bar and the binding read one field for exactly this
  // reason. The Confirm slot is named after the slab it activates, which is
  // SdMissingScreen's own rule (`{"", "RETRY", "", ""}`), and the two movers get
  // empty slots -- 36px each in the bar, never zero.
  vm_.hints = vm_.offersDelete ? std::array<std::string, 4>{"CLOSE", "SELECT", "UP", "DOWN"}
                               : std::array<std::string, 4>{"CLOSE", "OK", "", ""};
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
      // REACHED ON THE TWO SHAPES THAT DRAW THE SLAB, and unreachable on the third
      // WITHOUT A TEST HERE: the OutOfMemory shape has one row, so the focus can
      // never be kDelete and this line cannot run. Gating it on `offersDelete` as
      // well would be a second spelling of the same fact, free to disagree with the
      // row count -- see rowsFor().
      //
      // Making it INERT on `Unreadable` was considered and rejected, because those
      // two shapes differ only by a sentence and a reader meeting an inert slab
      // would have nothing to learn the rule from -- the `works only sometimes`
      // trap. That argument is about a slab that DRAWS; it does not reach a shape
      // whose slab is absent. If the card really is gone the removal
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
