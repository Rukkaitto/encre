#pragma once
#include "reader/focus_screen.h"
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
class DeleteConfirmScreen : public FocusScreen {
 public:
  explicit DeleteConfirmScreen(LibraryScreen& library);

  ScreenId id() const override { return ScreenId::DeleteConfirm; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const DeleteConfirmViewModel& vm() const { return vm_; }
  // focus()/setFocus() are FocusScreen's -- final, one mechanism. "No wake can
  // reach this screen" is a fact about the shell that this header must not
  // encode; the base class makes the pair unwritable by halves.

  // CONSTANT, so every focus move here is a partial repaint. Unlike the actions
  // panel, nothing this screen draws changes shape with the focus:
  // QuietTheme::renderDeleteConfirm sizes its panel from the caption's wrap and
  // the paragraph's, both of which are fixed once the screen exists, plus two
  // action slabs of kActionH that are both always drawn -- focus only decides
  // which one is filled and which is outlined, in the same box. Pinned by
  // test_partial_repaint.cpp rather than asserted here.
  uint32_t paintFootprint() const override { return 1; }

 private:
  enum Row { kCancel = 0, kDelete, kRowCount };

  void syncVm() override;

  LibraryScreen& library_;
  DeleteConfirmViewModel vm_;
};

}  // namespace reader
