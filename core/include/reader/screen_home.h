#pragma once
#include <vector>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// Home. Focus runs Continue (-1) then down through the menu rows; Confirm opens
// the focused row's screen.
//
// The hint LABELS are fixed, not focus-dependent: Home's board says
// READ / SELECT / UP / DOWN, and making Confirm read "OPEN" over the Library row
// would be a design change, which belongs in the board first.
class HomeScreen : public FocusScreen {
 public:
  // `targets` runs parallel to `vm.menu`: the screen each row opens. A row with
  // no screen yet (or one Phase 3 owns) gets no entry, and Confirm on it does
  // nothing. Kept out of the view-model because a view-model carries what the
  // theme draws, and the theme has no business knowing about ScreenId.
  HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets);

  ScreenId id() const override { return ScreenId::Home; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // focus()/setFocus() are FocusScreen's -- final, one mechanism. -1 is the
  // CONTINUE block, 0..n-1 the menu rows; the constructor picks the range (a
  // WithNone window, Noneless on the empty variant, which draws no CONTINUE).
  // This header once argued that setFocus was unreachable on Home because a
  // record naming the root pushes nothing -- true, and exactly why every wake
  // from Home landed on CONTINUE: the shell restores the ROOT's focus too, and
  // the base class is what keeps that reachable.
  const HomeViewModel& vm() const { return vm_; }

 private:
  void syncVm() override;

  HomeViewModel vm_;
  std::vector<ScreenId> targets_;
};

}  // namespace reader
