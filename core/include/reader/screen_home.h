#pragma once
#include <vector>

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// Home. Focus runs Continue (-1) then down through the menu rows; Confirm opens
// the focused row's screen.
//
// The hint LABELS are fixed, not focus-dependent: Home's board says
// READ / SELECT / UP / DOWN, and making Confirm read "OPEN" over the Library row
// would be a design change, which belongs in the board first.
class HomeScreen : public Screen {
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

  // -1 is the CONTINUE block, 0..n-1 the menu rows. This overrides
  // Screen::focus() now that the base declares one, which is stated rather than
  // left implicit: the signature already matched, so it became an override the
  // moment the virtual was added, and a reader of this header should not have to
  // work that out from app.h.
  //
  // setFocus is deliberately NOT overridden. The session record only ever
  // restores focus onto a screen the wake PUSHED, and a record naming Home
  // pushes nothing -- Home is already the root -- so a Home setFocus would be
  // unreachable code. Home's focus is still reported, because a record that says
  // where the user was is worth having in the log even where nothing acts on it.
  int focus() const override { return vm_.focusedMenuIndex; }
  const HomeViewModel& vm() const { return vm_; }

 private:
  // Returns Redraw only when the focus actually moved: at the end of the list a
  // press must not cost a 1.5 s panel refresh that changes nothing.
  Action moveFocus(int delta);

  HomeViewModel vm_;
  std::vector<ScreenId> targets_;
};

}  // namespace reader
