#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// The reader's menu (design/ReaderMenu.dc.html), opened by Activate on the page.
//
// AN OVERLAY, so the page stays visible under a veil: the reader has not left the
// book, they have asked it a question. Same shape as the Library's actions panel --
// App::render paints the Reader first and this draws its veil, its panel and its own
// hint bar over it -- and input still reaches only the top of the stack.
//
// IT DECLARES Mono, WHERE THE READER DECLARES Grayscale. Fidelity comes from the top
// screen, so this makes the menu's paint ONE waveform instead of three, and it makes a
// focus move inside it eligible for the overlay-only partial repaint (grayscale never
// is -- see App::canRenderTopOnly). The page underneath is drawn hard-thresholded for
// those frames, which is the trade: it is under a veil and the menu is chrome, where
// the page is the one thing on this device that wanted four levels.
//
// FIVE OF ITS SEVEN ROWS ARE NOT BUILT and are drawn anyway, with the focus skipping
// them -- Settings' rule, and its reasoning verbatim: a row that cannot be reached
// cannot mislead, where a row that focuses and then ignores SELECT is the silent no-op
// this project has been bitten by twice. An inert row is drawn EXACTLY as an unfocused
// live one; `ListRow::focusable` is about input, not appearance.
class ReaderMenuScreen : public FocusScreen {
 public:
  // `bookTitle` and `progress` are the panel's header -- the book's name and how far
  // through it the reader is. Passed in rather than read from the Reader beneath,
  // because an overlay that reached down the stack would be a second place that knows
  // how a Reader is shaped.
  ReaderMenuScreen(std::string bookTitle, std::string progress);

  ScreenId id() const override { return ScreenId::ReaderMenu; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const ReaderMenuViewModel& vm() const { return vm_; }

  // THE PANEL'S HEIGHT IS THE SUM OF ITS ROWS AND THEY ARE ALL ONE HEIGHT, so it does
  // not move when the focus does -- unlike the actions panel, whose focused row loses
  // its rule and makes the panel a pixel shorter. So a constant token is a true
  // promise here and every focus move takes the partial-repaint path.
  //
  // `1` rather than `0`: zero means "no promise" (Screen's default) and would refuse
  // the fast path for a panel that genuinely never moves.
  uint32_t paintFootprint() const override { return 1; }

  // The board's rows, in the board's order.
  //
  // `kNames` arrived from another branch's board edit (the character index it boards as
  // Names.dc.html), and this screen was built before it: six rows against the board's
  // seven, which `make compare` reported as "firmware ok" because it RENDERED. Measured
  // per pixel it was 13.02% against 3.02% before -- "ok" means the sim produced a frame,
  // not that the frame matches, and only the mismatch number says which.
  enum Row : int {
    kContents,
    kTypography,
    kGoToPage,
    kBookmarks,
    kNames,
    kAboutBook,
    kCloseBook,
  };
  static constexpr int kRowCount = 7;

 protected:
  void syncVm() override;
  bool focusable(int index) const override;

 private:
  ReaderMenuViewModel vm_{};
};

}  // namespace reader
