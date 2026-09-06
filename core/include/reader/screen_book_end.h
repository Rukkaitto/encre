#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// THE END OF A BOOK, from design/BookEnd.dc.html.
//
// It is PUSHED OVER THE READER, not swapped in for it, and that is what makes the
// board's BACK slot mean something: it returns to the last page, so a reader who
// wanted to re-read the ending can. It also keeps the book open, which is what the
// MARK AS FINISHED slab needs.
//
// It exists because paging forward off the last page of the last chapter was a DEAD
// BUTTON -- the walk ran out of spine entries and Gesture::Next returned none(), so
// the panel did not move and nothing was logged. This project has shipped a dead
// button twice and each time the note beside it was correct when it was written.
//
// NOT AN OVERLAY, checked against the board rather than assumed from the reader menu
// it sits near: it has no veil and no panel, and it draws its own header band and
// hint bar. So isOverlay() stays at its default false and this is a whole screen.
//
// IT TAKES FACTS, NOT A READER. The reader menu's `About this book` was built from
// the Library's focused row and so refused to open from a book reached through
// Home's CONTINUE; BookDetailsScreen::Facts is what fixed it, and this follows that
// shape rather than reaching down the stack into a screen it would then have to know
// the shape of.
class BookEndScreen : public FocusScreen {
 public:
  struct Facts {
    std::string bookTitle;
    std::string author;  // empty when the OPF did not say
    // Spine entries, cover included -- exactly the number Home already says `OF` in
    // `CH. 08 OF 92`. NOT a count of chapters with text: getting that means
    // paginating every entry, which is the ~49 s this line exists to avoid. Zero
    // means unknown, and the line is then not drawn at all rather than saying `0`.
    int chapterCount = 0;
    // Whether a Library is under the Reader. Decides the leaving slab's LABEL only;
    // the action is popTo(Library) either way, which stops at the root when there is
    // none. Handed in rather than discovered, because a screen that walked the stack
    // would be a second place that knows how the stack is shaped.
    bool libraryBeneath = false;
  };

  explicit BookEndScreen(const Facts& facts);

  ScreenId id() const override { return ScreenId::BookEnd; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const BookEndViewModel& vm() const { return vm_; }

  // The two slabs, in the board's order. Named rather than 0/1 for the reason the
  // item-actions overlay names its four: a row index is not a stable numbering, and
  // an index compared against a literal is one nobody can read.
  static constexpr int kFinish = 0;
  static constexpr int kLeave = 1;
  static constexpr int kRowCount = 2;

 private:
  void syncVm() override;

  BookEndViewModel vm_;
};

}  // namespace reader
