#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// The names the book has used (design/Names.dc.html, design/NamesEmpty.dc.html).
//
// A FULL SCREEN, not an overlay, on Contents' argument: a list you read and scroll,
// not a question about the page behind it.
//
// --- IT HAS NO ROWS YET, AND THAT IS THE WHOLE OF ITS BEHAVIOUR -----------------
//
// The card's name store is #156 and the display-time grouping is #157. Neither
// exists, so every instance renders the EMPTY VARIANT -- which is the honest answer
// for a book whose names have not been scanned, and is the same screen a reader
// legitimately meets at chapter one once the store does exist.
//
// SO IT IS NOT A `FocusScreen`, AND THAT IS DELIBERATE. `FocusScreen` exists to make
// the focus/setFocus pair structural for a list; a screen with no list has no focus
// to restore, and deriving from it now would mean carrying a ScrollWindow over an
// empty vector and answering `focusable()` about rows that do not exist. It becomes
// one when it gets rows, in the same change that adds them -- and that change has to
// touch this class anyway, so nothing is saved by guessing at the base class now.
//
// WHAT THE ROW IN THE READER MENU PROMISES IS THEREFORE TRUE. The menu's rule is
// that a row whose screen lands inside this release is drawn and live. Names does,
// so the row is drawn and it opens this screen; what is behind the door is "no names
// yet", not a no-op.
class NamesScreen : public Screen {
 public:
  NamesScreen();

  ScreenId id() const override { return ScreenId::Names; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const NamesViewModel& vm() const { return vm_; }

 private:
  NamesViewModel vm_{};
};

}  // namespace reader
