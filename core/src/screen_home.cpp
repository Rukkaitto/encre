#include "reader/screen_home.h"

#include "reader/theme.h"

namespace reader {

HomeScreen::HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets)
    : vm_(std::move(vm)),
      targets_(std::move(targets)),
      // WithNone: -1 is the CONTINUE block, a place the user can be, not the
      // absence of a selection. The view-model may arrive with a focus already
      // set -- the goldens author one -- so it is adopted rather than reset.
      //
      // EXCEPT ON THE EMPTY VARIANT, which draws no CONTINUE block: its first
      // hint slot is empty because there is nothing to read, so a focus on -1
      // would be a selection on an invisible row with a blank action. Building the
      // ring Noneless is the model being right, and it closes both ways in at
      // once -- Up from LIBRARY, which was reachable before lists wrapped, and
      // Down off the last menu row, which wrapping added.
      focus_(static_cast<int>(vm_.menu.size()),
             vm_.libraryEmpty ? Focus::Noneless : Focus::WithNone) {
  focus_.set(vm_.focusedMenuIndex);
  vm_.focusedMenuIndex = focus_.index();
}

bool HomeScreen::syncFocus(bool moved) {
  // The view-model is what the theme draws, so the focus has to be mirrored into
  // it. That mirror is the whole of what this screen now does about focus: the
  // range, the clamp and the "did anything move" answer are all Focus's.
  if (moved) vm_.focusedMenuIndex = focus_.index();
  return moved;
}

bool HomeScreen::setFocus(int index) { return syncFocus(focus_.set(index)); }

// Redraw only when the focus actually moved: at the end of the list a press must
// not cost a 1.5 s panel refresh that changes nothing.
Action HomeScreen::moveFocus(int delta) {
  return syncFocus(focus_.move(delta)) ? Action::redraw() : Action::none();
}

Action HomeScreen::onEvent(const InputEvent& ev) {
  // Home binds no holds, so a Long here means the mask and the view model
  // disagree. Ignoring it keeps that visible as a dead button rather than
  // papering over it by treating the hold as a press.
  if (ev.kind != PressKind::Short) return Action::none();

  switch (ev.button) {
    case Button::Down:
      return moveFocus(+1);
    case Button::Up:
      return moveFocus(-1);
    case Button::Confirm: {
      const int i = vm_.focusedMenuIndex;
      // Continue: the Reader is Phase 3.
      if (i < 0) return Action::none();
      if (i >= static_cast<int>(targets_.size())) return Action::none();
      return Action::push(targets_[static_cast<size_t>(i)]);
    }
    // Home's board binds Back to "Read" (spec 4.1: there is nothing to go back
    // to), which opens the Reader -- Phase 3.
    case Button::Back:
    default:
      return Action::none();
  }
}

void HomeScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderHome(fb, fonts, vm_, plane);
}

}  // namespace reader
