#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// design/WifiNetworkActions.dc.html -- what holding Confirm on a saved network
// opens.
//
// ITS OWN ScreenId, WHICH IS A REAL COST AND IS NOT AVOIDABLE: ItemActions
// reads the LIBRARY's focused row, so it cannot be reused for a list of
// networks. This is what takes the connect flow from five screens to six.
//
// ONE ROW, DELIBERATELY, and the overlay IS the confirmation step. Forgetting a
// network costs you retyping a password, where deleting a book is irreversible
// and gets DeleteConfirm; a hold that destroyed a credential with nothing in
// between would be the only such action in this firmware. It is also where
// `Connect now` and `Make automatic` go when there is a reason for them.
//
// IT TAKES FACTS, NOT A REFERENCE TO THE SCREEN BELOW. BookDetailsScreen::Facts
// is the precedent and its reason applies here: a screen built from its
// parent's focused row works only where that parent exists, and "a button that
// works only sometimes is worse than one that never does, because nobody can
// learn the rule".
class WifiNetworkActionsScreen : public FocusScreen {
 public:
  struct Facts {
    std::string ssid;
    bool automatic = false;
  };

  explicit WifiNetworkActionsScreen(Facts facts);

  ScreenId id() const override { return ScreenId::WifiNetworkActions; }
  // AN OVERLAY: the screen beneath stays visible under a veil, and App::render
  // walks down to the topmost non-overlay before painting upward.
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const WifiNetworkActionsViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }

  // Whether the reader confirmed the forget. The shell reads it after the pop,
  // because removing a network touches NVS and the list below -- which is the
  // shell's business, exactly as a delete's consequences are.
  bool forgetChosen() const { return forget_; }

 protected:
  void syncVm() override;

 private:
  Facts facts_;
  WifiNetworkActionsViewModel vm_;
  bool forget_ = false;
};

}  // namespace reader
