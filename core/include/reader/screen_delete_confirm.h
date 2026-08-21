#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

class LibraryScreen;

// The delete confirmation (design/DeleteConfirm.dc.html), reached from the item
// actions overlay's `Delete...`.
//
// An overlay, like the panel it came from. It holds a REFERENCE to the Library
// rather than a copy of the selection, because unlike the actions panel it does
// not merely display the book -- it deletes it, and then the list under it has to
// be re-read. A copied index would be a second answer to "which book", and the
// one moment the two could disagree is the one moment it matters.
//
// Spec 4.0: delete "has a confirmation step and never erases reading progress".
// So this removes exactly one file and touches `/.reader/state/` not at all. A
// book that comes back -- a card edited on a computer, a file copied again --
// should still know where you were.
class DeleteConfirmScreen : public Screen {
 public:
  explicit DeleteConfirmScreen(LibraryScreen& library);

  ScreenId id() const override { return ScreenId::DeleteConfirm; }
  bool isOverlay() const override { return true; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const DeleteConfirmViewModel& vm() const { return vm_; }
  int focus() const { return vm_.focusedAction; }

 private:
  enum Row { kCancel = 0, kDelete, kRowCount };

  Action moveFocus(int delta);

  LibraryScreen& library_;
  DeleteConfirmViewModel vm_;
};

}  // namespace reader
