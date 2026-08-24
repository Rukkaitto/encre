#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

class LibraryScreen;

// Book details (design/BookDetails.dc.html), reached from the item actions
// overlay.
//
// NOT an overlay, and that was checked against the board rather than assumed
// from its neighbours: it has no veil div, no panel, and its own header band and
// hint bar. The `.dim-veil` rule in its stylesheet is declared and never used.
// So `isOverlay()` is left at its default false and this draws a whole screen.
//
// It is read-only: nothing on it can be selected, which is why its board draws
// three of its four hint slots as the dead-button placeholder. Back is the only
// binding.
//
// Six of its rows need EPUB metadata or per-book state and are therefore blank
// until Phase 3. Two are real today -- the file's size and where it lives -- and
// one is honestly zero: there are no bookmarks because there is nothing to make
// one with.
class BookDetailsScreen : public Screen {
 public:
  // EVERYTHING THIS SCREEN DRAWS, AS VALUES, and that is what lets the reader menu's
  // `About this book` row work at all.
  //
  // It was built from the LIBRARY's focused row, which is fine from the Library and
  // wrong from the Reader: a reader who arrived through Home's CONTINUE has no Library on
  // the stack, so the factory refused the push and the row did nothing. Making it
  // focusable anyway would have been a button that works only sometimes, which is worse
  // than one that never does -- nobody can learn the rule.
  //
  // So the screen takes facts. The Library can answer them from a row and the Reader can
  // answer them from the book it has open, and neither has to know how the other is
  // shaped.
  struct Facts {
    std::string title;      // as shown -- the leaf name without its extension
    std::string author;     // from the OPF; empty leaves the row blank
    std::string fileName;   // for the format band: the extension, shouted
    std::string directory;  // for the Location row, without a trailing slash
    std::string progress;   // "31%", or empty for a book never opened
    std::string chapter;    // the chapter name at the saved position, or empty
    uint32_t bytes = 0;     // the file's size, for the File size row
  };

  explicit BookDetailsScreen(Facts facts);

  ScreenId id() const override { return ScreenId::BookDetails; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const BookDetailsViewModel& vm() const { return vm_; }

 private:
  BookDetailsViewModel vm_;
};

}  // namespace reader
