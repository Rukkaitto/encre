#include "reader/screen_item_actions.h"

#include "reader/screen_library.h"
#include "reader/theme.h"

namespace reader {

ItemActionsScreen::ItemActionsScreen(const LibraryScreen& library) {
  const LibraryItem* item = library.focusedItem();
  // A null focus should not be reachable -- the Library refuses the hold on an
  // empty list -- but an overlay captioned with a blank name is a better failure
  // than one that dereferences nothing.
  if (item != nullptr) {
    vm_.title = item->entry.title;
    vm_.status = item->progress;
  }
  // The board's four rows, in the board's order, and its chevrons: Open and Book
  // details lead somewhere, Mark as finished and Delete... act in place.
  vm_.actions = {{"Open", true}, {"Book details", true}, {"Mark as finished", false},
                 {"Delete\xE2\x80\xA6", false}};
  vm_.focusedAction = kOpen;
  // The board's own labels. CLOSE rather than BACK, because what Back does here
  // is dismiss a panel rather than leave a screen -- and no holds, so no slot
  // shows a ring.
  vm_.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
}

Action ItemActionsScreen::moveFocus(int delta) {
  int next = vm_.focusedAction + delta;
  // Clamped, not wrapped: one rule for every list in this firmware.
  if (next < 0) next = 0;
  if (next > kRowCount - 1) next = kRowCount - 1;
  if (next == vm_.focusedAction) return Action::none();
  vm_.focusedAction = next;
  return Action::redraw();
}

Action ItemActionsScreen::onEvent(const InputEvent& ev) {
  // No holds are bound and no slot shows a ring, so a Long here means the mask
  // and the view-model have drifted. Ignoring it keeps that visible.
  if (ev.kind != PressKind::Short) return Action::none();

  switch (ev.button) {
    case Button::Down:
      return moveFocus(+1);
    case Button::Up:
      return moveFocus(-1);
    case Button::Back:
      return Action::pop();
    case Button::Confirm:
      switch (vm_.focusedAction) {
        case kOpen:
          // The Reader is Phase 3, exactly as Confirm on a Library row is.
          return Action::none();
        case kDetails:
          return Action::push(ScreenId::BookDetails);
        case kFinished:
          // NOTHING, deliberately. "Finished" is per-book state, and per-book
          // state does not exist: `/.reader/state/` has no format yet because
          // there is no Reader to write one. Inventing a file here would commit
          // 2C-3 and Phase 3 to agreeing with a schema chosen by the screen that
          // needed it least -- and a wrong schema on the card is worse than a
          // button that does nothing, because the card outlives the firmware.
          return Action::none();
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
