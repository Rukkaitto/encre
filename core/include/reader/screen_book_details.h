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
  explicit BookDetailsScreen(const LibraryScreen& library);

  ScreenId id() const override { return ScreenId::BookDetails; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const BookDetailsViewModel& vm() const { return vm_; }

 private:
  BookDetailsViewModel vm_;
};

}  // namespace reader
