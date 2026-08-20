#include "reader/screen_home.h"

#include "reader/theme.h"

namespace reader {

HomeScreen::HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets)
    : vm_(std::move(vm)), targets_(std::move(targets)) {}

Action HomeScreen::moveFocus(int delta) {
  const int last = static_cast<int>(vm_.menu.size()) - 1;
  int next = vm_.focusedMenuIndex + delta;
  // Clamp rather than wrap. Home's menu is short enough that wrapping would be
  // pleasant, but Library will hold hundreds of books and a list that jumps
  // silently from the last item to the first is indistinguishable from a stuck
  // button. One rule for every list.
  if (next < -1) next = -1;
  if (next > last) next = last;
  if (next == vm_.focusedMenuIndex) return Action::none();
  vm_.focusedMenuIndex = next;
  return Action::redraw();
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
