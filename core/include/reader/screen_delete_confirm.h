#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// The delete confirmation (design/DeleteConfirm.dc.html), reached from the item
// actions overlay's `Delete...` and from BookError's `DELETE FILE...`.
//
// An overlay, like the panel it came from.
class DeleteConfirmScreen : public FocusScreen {
 public:
  // FACTS, NOT A REFERENCE. This screen used to hold a LibraryScreen& and act
  // through deleteFocused(), which made it unreachable from Home's CONTINUE -- and
  // CONTINUE is the likeliest real corruption path, because it is a book the reader
  // was part-way through. BookDetailsScreen::Facts solved the identical problem for
  // the identical reason.
  //
  // Spec 4.0: delete "has a confirmation step and never erases reading progress".
  // The shell removes exactly one file and touches /.reader/state/ not at all -- a
  // book that comes back should still know where you were.
  struct Facts {
    std::string path;         // absolute on the filesystem
    std::string displayName;  // for the caption
    // Where to land once the file is gone. From the actions panel that is the
    // Library; from BookError it is the Library OR Home, which is the case a
    // LibraryScreen& could not express.
    ScreenId returnTo = ScreenId::Library;
  };

  explicit DeleteConfirmScreen(Facts facts);

  ScreenId id() const override { return ScreenId::DeleteConfirm; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const DeleteConfirmViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }
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
  //
  // Facts did not change that, and it could not: a push is never a partial repaint
  // (App::transition() is the signal), so two instances carrying different captions
  // can never be compared against one frame record. The token only has to hold
  // across focus moves within one screen's life.
  uint32_t paintFootprint() const override { return 1; }

 private:
  enum Row { kCancel = 0, kDelete, kRowCount };

  void syncVm() override;

  Facts facts_;
  DeleteConfirmViewModel vm_;
};

}  // namespace reader
