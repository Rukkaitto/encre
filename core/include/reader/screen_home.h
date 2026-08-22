#pragma once
#include <vector>

#include "reader/app.h"
#include "reader/focus.h"
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
  // setFocus IS overridden, and this comment used to argue at length that it
  // must not be: the session record only restores onto a screen the wake pushed,
  // a record naming Home pushes nothing, so a Home setFocus would be unreachable.
  // The premise was true and the conclusion was the wrong way round -- the
  // restore ladder skipped Home BECAUSE nothing here could accept a focus, so
  // Home stored where the user was on every sleep and woke on CONTINUE every
  // time. It is the root that gets its focus set instead of a pushed screen (see
  // the Home branch of the restore ladder in shell/src/main.cpp), which is one
  // extra branch there rather than a reason to drop the value on the floor.
  //
  // The bool is ScrollWindow's contract: "something moved", not "the restore was
  // accepted". Restoring CONTINUE onto a Home that is already on CONTINUE is a
  // perfectly good restore and returns false.
  int focus() const override { return vm_.focusedMenuIndex; }
  bool setFocus(int index) override;
  const HomeViewModel& vm() const { return vm_; }

 private:
  Action moveFocus(int delta);
  // Mirrors focus_ into the view-model the theme draws, and passes the "did
  // anything move" answer through. See screen_home.cpp.
  bool syncFocus(bool moved);

  HomeViewModel vm_;
  std::vector<ScreenId> targets_;
  Focus focus_;
};

}  // namespace reader
