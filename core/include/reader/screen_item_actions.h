#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

class LibraryScreen;

// The item actions overlay (design/LibraryActions.dc.html), reached by holding
// Confirm on a Library row.
//
// It is an OVERLAY: the board draws a centred panel over a still-visible, veiled
// Library, so App::render paints the Library first and this draws its veil, its
// panel and its own hint bar on top. Input still comes only to the top of the
// stack -- a press here must not move a focus the user can see but cannot reach.
//
// It is constructed from the Library it floats over, and takes a COPY of what it
// displays. The focus underneath cannot move while this is up (the parent
// receives no events), so a copy cannot go stale -- and the one thing that does
// change the list, a delete, pops this screen as part of doing it.
class ItemActionsScreen : public Screen {
 public:
  explicit ItemActionsScreen(const LibraryScreen& library);

  ScreenId id() const override { return ScreenId::ItemActions; }
  bool isOverlay() const override { return true; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const ItemActionsViewModel& vm() const { return vm_; }
  // Which of the four action rows is selected. An override of Screen::focus()
  // since the base declared one, and marked so. No setFocus: this panel is only
  // ever reached by a hold on a live Library, so there is no wake that can put
  // the user back on it -- the factory refuses to build one with no Library
  // under it, which is the correct answer and not a gap.
  int focus() const override { return vm_.focusedAction; }

 private:
  // The board's four rows, in its order. An enum rather than comparing the
  // label, because a label is content and Confirm's behaviour is not.
  enum Row { kOpen = 0, kDetails, kFinished, kDelete, kRowCount };

  Action moveFocus(int delta);

  ItemActionsViewModel vm_;
};

}  // namespace reader
