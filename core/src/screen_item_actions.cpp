#include "reader/screen_item_actions.h"

#include "reader/screen_library.h"
#include "reader/theme.h"

namespace reader {

ItemActionsScreen::ItemActionsScreen(const LibraryScreen& library)
    : FocusScreen(kRowCount, kRowCount) {
  const LibraryItem* item = library.focusedItem();
  // A null focus should not be reachable -- the Library refuses the hold on an
  // empty list -- but an overlay captioned with a blank name is a better failure
  // than one that dereferences nothing.
  if (item != nullptr) {
    vm_.title = std::string(item->entry.title());
    vm_.status = item->progress;
  }
  // The board's four rows, in the board's order, and its chevrons: Open and Book
  // details lead somewhere, Mark as finished and Delete... act in place.
  vm_.actions = {{"Open", true}, {"Book details", true}, {"Mark as finished", false},
                 {"Delete\xE2\x80\xA6", false}};
  // The board's own labels. CLOSE rather than BACK, because what Back does here
  // is dismiss a panel rather than leave a screen -- and no holds, so no slot
  // shows a ring.
  vm_.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // The base's focus starts on the first row, which is kOpen -- the board's own
  // starting selection. The mirror below is all the focus state there is.
  syncVm();
}

void ItemActionsScreen::syncVm() { vm_.focusedAction = focus(); }

Action ItemActionsScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Back:
      return Action::pop();
    case Gesture::Activate:
      switch (vm_.focusedAction) {
        case kOpen:
          // OPENS THE BOOK. This said "the Reader is Phase 3, exactly as Confirm on a
          // Library row is" -- and Confirm on a Library row opens a book now, so this
          // row had become a dead button on a shipped screen. Action::open() means "the
          // book I have selected", which is the Library's focused row either way: this
          // overlay is built from it and cannot outlive its selection changing.
          return Action::open();
        case kDetails:
          return Action::push(ScreenId::BookDetails);
        case kFinished:
          // LATCHED FOR THE SHELL, exactly as kOpen's Action::open() is: the write is
          // to the card and storage is not core/'s. Two screens ask for this and mean
          // different books -- this one means the Library's focused row, BookEnd means
          // the open book -- and the shell resolves it the way handleOpen already does.
          //
          // This returned none() for two phases behind a comment saying per-book state
          // "does not exist ... because there is no Reader to write one". There is,
          // and /.reader/state/ has a format, so the comment's own expiry date had
          // passed and the row was a dead button on a shipped screen.
          //
          // NO POP HERE. The board gives this row no chevron -- "Mark as finished and
          // Delete... act in place" -- and the shell pops the overlay after the write,
          // so the Library underneath is repainted with the row reading DONE.
          return Action::finish();
        case kDelete:
          return Action::push(ScreenId::DeleteConfirm);
        default:
          return Action::none();
      }
    default:
      return Action::none();
  }
}

void ItemActionsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                               Plane plane) const {
  theme.renderItemActions(fb, fonts, vm_, plane);
}

}  // namespace reader
