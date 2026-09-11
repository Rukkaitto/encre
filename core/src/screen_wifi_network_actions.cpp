#include "reader/screen_wifi_network_actions.h"

#include "reader/theme.h"

namespace reader {

WifiNetworkActionsScreen::WifiNetworkActionsScreen(Facts facts)
    : FocusScreen(1, 1), facts_(std::move(facts)) {
  vm_.caption = facts_.ssid;
  vm_.captionValue = facts_.automatic ? "AUTO" : "SAVED";
  ListRow row;
  row.label = "Forget network";
  row.focusable = true;
  // NO CHEVRON: it acts in place rather than disclosing a screen, and
  // ListRow::discloses is explicit precisely so that is not inferred from the
  // empty value.
  vm_.rows.push_back(row);
  syncVm();
}

void WifiNetworkActionsScreen::syncVm() {
  vm_.focusedRow = focus();
  // FORGET rather than SELECT, which is Settings' rule that the label follows
  // the focused row -- and here it doubles as the warning: the word says what
  // the press does. Up and Down are dead slots, because one row means a move
  // that cannot change anything, and a mark over a button with no action is an
  // affordance for something that is not there.
  vm_.hints = {"BACK", "FORGET", "", ""};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

Action WifiNetworkActionsScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      return Action::pop();
    case Gesture::Activate:
      // The overlay is the confirmation step, so this is the forget. The shell
      // does the removal -- it touches NVS and the list below, which is not
      // this screen's business -- and it does it BEFORE the pop, reading
      // forgetChosen() off a screen that is still standing. This returned
      // Action::pop() and offered that getter, which dispatch's
      // `stack_.pop_back()` made unreadable. See Action::wifi().
      forget_ = true;
      return Action::wifi();
    default:
      // Up and Down deliberately do nothing: one row, so a move would report
      // no change anyway, and the bar promises neither.
      return Action::none();
  }
}

void WifiNetworkActionsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                      Plane plane) const {
  theme.renderWifiNetworkActions(fb, fonts, vm_, plane);
}

}  // namespace reader
