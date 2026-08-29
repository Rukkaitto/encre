#include "reader/screen_home.h"

#include "reader/theme.h"

namespace reader {

// WithNone: -1 is the CONTINUE block, a place the user can be, not the absence
// of a selection.
//
// EXCEPT WHERE THERE IS NOTHING TO CONTINUE, which draws no CONTINUE block: its
// first hint
// slot is empty because there is nothing to read, so a focus on -1 would be a
// selection on an invisible row with a blank action. Building the ring Noneless
// is the model being right, and it closes both ways in at once -- Up from
// LIBRARY, which was reachable before lists wrapped, and Down off the last menu
// row, which wrapping added.
HomeScreen::HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets)
    : FocusScreen(static_cast<int>(vm.menu.size()), static_cast<int>(vm.menu.size()),
                  vm.nothingToContinue ? Focus::Noneless : Focus::WithNone),
      vm_(std::move(vm)),
      targets_(std::move(targets)) {
  // The view-model may arrive with a focus already set -- the goldens author one
  // -- so it is adopted rather than reset; the mirror is then re-asserted
  // unconditionally, because setFocus reports "moved" and an unmoved adoption
  // still has to leave the vm and the window agreeing.
  setFocus(vm_.focusedMenuIndex);
  syncVm();
  // From whatever view model arrived, because Home's hints are built outside the
  // screen (demoHomeVm, homeVmForCard). Home binds no hold today and the empty
  // variant binds none either, so this is zero -- but it is DERIVED from the bar
  // rather than assumed, which is the point: the day a Home hint grows a ring, the
  // binding follows it without anyone remembering to add one.
  declareHints(vm_.holds);
}

// The mirror is the whole of what this screen still does about focus: the range,
// the clamp and the "did anything move" answer are all FocusScreen's.
void HomeScreen::syncVm() { vm_.focusedMenuIndex = focus(); }

Action HomeScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Activate: {
      const int i = vm_.focusedMenuIndex;
      // CONTINUE, which is focus -1. It answers Action::open() -- "open the book I
      // have selected" -- because opening a book is reading a file off the card and
      // storage is not core/'s; the shell resolves WHICH book from the same pointer
      // that filled this reading column in.
      //
      // It cannot fire on the no-reading-column variants: those build the focus ring
      // Noneless, so -1 is unreachable there. The model prevents it rather than a
      // guard here, which is why there is no second check.
      if (i < 0) return Action::open();
      if (i >= static_cast<int>(targets_.size())) return Action::none();
      return Action::push(targets_[static_cast<size_t>(i)]);
    }
    // HOME'S BOARD BINDS BACK TO `READ` -- spec 4.1: there is nothing to go back to
    // from the root, so the slot carries the one action worth a shortcut. Same
    // action as CONTINUE, from a button instead of a selection.
    //
    // Gated on there being a book, because the no-reading-column variants draw an
    // EMPTY first hint slot: a bar that promises nothing must not do something.
    case Gesture::Back:
      return vm_.nothingToContinue ? Action::none() : Action::open();
    default:
      return Action::none();
  }
}

void HomeScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderHome(fb, fonts, vm_, plane);
}

void HomeScreen::setBattery(int percent, bool charging) {
  vm_.batteryPercent = percent;
  vm_.batteryCharging = charging;
}

}  // namespace reader
