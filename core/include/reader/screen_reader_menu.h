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
// EVERY ONE OF ITS THREE ROWS RESPONDS, and that took two rows leaving rather than
// arriving. Settings' rule -- every board row is DRAWN and the focus SKIPS the ones
// that cannot act, because a row that cannot be reached cannot mislead where a row
// that focuses and then ignores SELECT is the silent no-op this project has been
// bitten by twice -- has no instance left on this screen. `focusable()` and
// `ListRow::focusable` stay, because the rule is the screen's and the next unbuilt
// V1 row gets it for free.
//
// THAT RULE HAS A LIMIT, AND IT WAS REACHED TWICE: `Bookmarks` (#3) and then `Names`
// (#73). Skipping the focus keeps an unbuilt row from misleading a reader who presses
// it; it does nothing about the row itself promising a feature the release does not
// have. WHICH RELEASE THE ROW IS WAITING ON is the whole distinction -- a row whose
// screen lands inside this release is drawn and skipped, and a row whose screen moved
// out of it is cut from the board and from here. Bookmarks moved to V1.1; the whole
// Names family is V2 (three boarded cards -- the per-chapter index, the list screen,
// and the alias-row overflow). Names was drawn and inert under the FIRST reading of
// that rule, on the belief that it was waiting on a screen inside V1; it was not, so
// it is Bookmarks' case and not Typography's, and it comes back with its screen.
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

  // THE PANEL'S ROWS ARE ALL ONE HEIGHT, so this claims a constant footprint and every
  // focus move takes the partial-repaint path. `1` rather than `0`: zero means "no
  // promise" (Screen's default) and would refuse the fast path outright.
  //
  // THE CLAIM IS NOT ACTUALLY TRUE AND #68 IS THE OPEN CARD FOR IT. The rows are one
  // height, but `renderReaderMenu` sizes the panel through
  // `panelRowHeight(rowRuleFor(i, rows, focused))`, and `rowRuleFor` suppresses the
  // rule for the focused row AND for the last row -- so focusing the LAST row is the
  // one case where two suppressions coincide and the centred panel moves a pixel.
  // Measured on the X3: panel top 213 on Contents and Typography, 212 on About this
  // book. That is the actions panel's own defect, which `ItemActions::paintFootprint`
  // counts borderless rows for and this does not.
  //
  // CUTTING `Names` DID NOT MOVE THAT. It was never focusable and was never last, so
  // it always drew its rule: removing it takes 72px off the panel in every state and
  // leaves the focusable set, and therefore every per-state delta, exactly as it was.
  uint32_t paintFootprint() const override { return 1; }

  // The board's rows, in the board's order.
  //
  // `kGoToPage`, `kCloseBook`, `kBookmarks` and `kNames` are GONE, and the board states
  // why: a reflowable book has no stable page to go to, Back from the page already
  // closes the book, bookmarks are V1.1, and the whole Names family is V2. The enum is
  // not a stable numbering to be preserved -- the ONE thing that persists a row index
  // is FocusScreen's restore, and it refuses an index it cannot land on, which is
  // exactly the case a shrunk table creates. This is the third cut for that reason and
  // it needed no more care than the first two.
  //
  // `kNames` IS ALSO THE ROW THAT PROVED "ok" IS NOT A FIDELITY CHECK, on the way in
  // and again on the way out. It arrived from another branch's board edit and this
  // screen was built before it -- six rows against the board's seven -- and `make
  // compare` reported "firmware ok" because the simulator RENDERED, at 13.02%
  // per pixel against 3.02%. Cutting it needed the same measurement in reverse.
  enum Row : int {
    kContents,
    kTypography,
    kAboutBook,
  };
  static constexpr int kRowCount = 3;

 protected:
  void syncVm() override;
  bool focusable(int index) const override;

 private:
  ReaderMenuViewModel vm_{};
};

}  // namespace reader
