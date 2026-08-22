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
  // From whatever view model arrived, because Home's hints are built outside the
  // screen (demoHomeVm, homeVmForCard). Home binds no hold today and the empty
  // variant binds none either, so this is zero -- but it is DERIVED from the bar
  // rather than assumed, which is the point: the day a Home hint grows a ring, the
  // binding follows it without anyone remembering to add one.
  declareHints(vm_.holds);
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

Action HomeScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Activate: {
      const int i = vm_.focusedMenuIndex;
      // Continue: the Reader is Phase 3.
      if (i < 0) return Action::none();
      if (i >= static_cast<int>(targets_.size())) return Action::none();
      return Action::push(targets_[static_cast<size_t>(i)]);
    }
    // Home's board binds Back to "Read" (spec 4.1: there is nothing to go back
    // to), which opens the Reader -- Phase 3.
    case Gesture::Back:
    default:
      return Action::none();
  }
}

void HomeScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderHome(fb, fonts, vm_, plane);
}

}  // namespace reader
