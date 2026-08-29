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
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // focus()/setFocus() are FocusScreen's -- final, one mechanism. -1 is the
  // CONTINUE block, 0..n-1 the menu rows; the constructor picks the range (a
  // WithNone window, Noneless on the empty variant, which draws no CONTINUE).
  // This header once argued that setFocus was unreachable on Home because a
  // record naming the root pushes nothing -- true, and exactly why every wake
  // from Home landed on CONTINUE: the shell restores the ROOT's focus too, and
  // the base class is what keeps that reachable.
  const HomeViewModel& vm() const { return vm_; }

  // THE BATTERY MOVES AFTER THE VIEW MODEL IS BUILT, which no other Home field
  // does: Home's vm is constructed once and only rebuilt at boot, on a wake, and
  // on a Back out of a book, while the charge changes continuously and the shell
  // re-reads it before every Home paint. Same shape and same reason as
  // LibraryScreen::refreshProgress().
  //
  // It deliberately does NOT touch the focus. It runs on the paint path, so
  // moving a selection here would move one the user never touched.
  void setBattery(int percent, bool charging);

 private:
  void syncVm() override;

  HomeViewModel vm_;
  std::vector<ScreenId> targets_;
};

}  // namespace reader
