#pragma once
#include "reader/focus_screen.h"
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
class ItemActionsScreen : public FocusScreen {
 public:
  explicit ItemActionsScreen(const LibraryScreen& library);

  ScreenId id() const override { return ScreenId::ItemActions; }
  bool isOverlay() const override { return true; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const ItemActionsViewModel& vm() const { return vm_; }
  // focus()/setFocus() are FocusScreen's -- final, one mechanism. This header
  // used to argue that setFocus could be left off because no wake can land here;
  // the premise was a fact about the shell's restore ladder, and encoding it in
  // a core/ screen is how a change over there leaves a screen silently one-way.
  // The base class makes that argument unwritable.

  // THE ONE THING THAT MOVES THIS PANEL'S BOX IS HOW MANY OF ITS ROWS HAVE A
  // RULE, so that count is the token. QuietTheme::renderItemActions sums
  // `panelRowHeight(i != focusedAction && i != rows - 1)`, and the panel is
  // centred on the screen, so a different count is a different height and a
  // different top edge; everything else the box depends on -- the panel's width,
  // the caption's wrap, the hint bar -- is fixed while this screen is up, because
  // it took a copy of what it displays.
  //
  // MEASURED, and it is why this cannot just return a constant: moving the focus
  // OFF the last row goes from one borderless row to two, so the panel loses a
  // pixel of height and its top edge drops one pixel. A partial repaint across
  // that transition leaves the old top border standing -- 226 pixels at y=212 on
  // the X3, a hairline hugging the panel's edge, and the veil only takes 5 of
  // every 9 of them away. The count differs across exactly that transition and no
  // other, so App refuses it and repaints the stack; the other three focus moves
  // take the fast path. test_partial_repaint.cpp walks every pair.
  uint32_t paintFootprint() const override {
    const int rows = static_cast<int>(vm_.actions.size());
    uint32_t borderless = 0;
    for (int i = 0; i < rows; ++i)
      if (i == vm_.focusedAction || i == rows - 1) ++borderless;
    // Offset by one, because zero is Screen's "no promise".
    return 1u + borderless;
  }

 private:
  // The board's four rows, in its order. An enum rather than comparing the
  // label, because a label is content and Confirm's behaviour is not.
  enum Row { kOpen = 0, kDetails, kFinished, kDelete, kRowCount };

  void syncVm() override;

  ItemActionsViewModel vm_;
};

}  // namespace reader
