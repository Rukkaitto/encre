#pragma once
#include <cstdint>
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// WHY THE BOOK WOULD NOT OPEN, in the only vocabulary a screen may have.
//
// openBook has four reasons and they are not one event. Three are parse failures --
// a zip that is not one, an OPF that will not parse, a spine with nothing in it --
// and the fourth is fs.openRead() returning null, which is a file that is gone or a
// card that is. SdFileSystem::openRead does NOT call noteCardGone(); only a handle
// read that comes up short does, so a card pulled between the Library's listing and
// the press is noticed by pollCardPresence between 2s (the fast probe) and 25s (the
// FAT-scan backstop) -- and for that whole window a single sentence would tell the
// reader a perfectly healthy book "appears to be damaged".
//
// A false claim is worse than an absent one. That is the call this project already
// makes for an unread battery gauge (-1, not 0%), for a book with no reading
// position (no demo substitute), and for the charging bolt that spends a refresh on
// the unplug edge rather than staying wrong on glass.
//
// AND THERE IS A THIRD NOW, WHICH IS THE SAME ARGUMENT ARRIVING ONE REFUSAL LATER.
// `openBook` can run out of memory: the open path grows five containers from numbers
// a FILE states, and until the heap guards landed a `reserve` that could not allocate
// was `abort()` with no message -- reported off an X3 as a book that crashed the
// firmware and then opened normally on the second press, with a 232-book library
// resident underneath and 13,696 bytes of heap left over a successful open.
//
// NEITHER EXISTING SHAPE MAY CARRY IT. `Damaged` says the bytes are not a book, and
// they are; `Unreadable` says the card would not answer, and it did. The book is
// fine and the device was momentarily short -- which is `CoverResult::OutOfMemory`'s
// distinction, one screen over, and the same reason that enum has six values rather
// than a bool. So it gets its own board (design/BookErrorMemory.dc.html) and its own
// sentence.
//
// AND THIS SHAPE HAS NO `DELETE FILE...` SLAB. It shipped with one, drawn and live on
// all three shapes, and this comment recorded that as owed an owner's opinion rather
// than settled. It is settled: the file is FINE, so offering to delete a good book to
// fix a transient shortage is a nudge in the wrong direction and a reader might take
// it. `Damaged` and `Unreadable` keep theirs -- on those two, wanting the file gone is
// a reasonable thing to want. `HomeEmpty` is the precedent, whose action slab was
// removed rather than left standing when Wi-Fi was cut: `a primary action that cannot
// work is worse than none`.
//
// ABSENT, NOT INERT, AND THAT IS THE WHOLE LICENCE. A slab that DRAWS and does
// nothing is the `works only sometimes` trap, which is the recorded reason the slab is
// live on `Unreadable` -- those two shapes differ only by a sentence, so a reader
// meeting a dead slab has nothing to learn the rule from. A slab that is not there
// teaches nothing because there is nothing to press.
//
// TWO OF THE THREE OBJECTIONS THIS COMMENT USED TO RAISE WERE ALREADY FALSE. `a fourth
// board` -- the third board is the one edited, and nothing was added. `a panel whose
// height depends on which refusal it is reporting` -- it already did, and
// paintFootprint's own comment below says so.
enum class BookErrorReason : uint8_t { Damaged, Unreadable, OutOfMemory };

// WHICH SHAPE `openBook`'s REASON IS, and it lives here rather than in the shell
// because it is the whole of the mapping from developer English to the only
// vocabulary the panel has -- and `shell/` has no test harness, which is where five
// of this project's bugs have hidden. The shell had a bare `strcmp` against a
// literal spelled twice; a third shape would have meant a second one beside it, and
// a fourth that nobody remembered to add reads as "damaged" on a healthy file.
//
// `why` is `openBook`'s out-parameter. A null or empty string is `Damaged`, which is
// the conservative answer: a refusal that would not say why is at least not a claim
// about the card or about the heap.
BookErrorReason bookErrorReasonFor(const char* why);

// The corrupt-book dialog (design/BookError.dc.html).
//
// An overlay, so App::render paints whatever is under it -- the Library, which is
// what the board draws, or HOME, which no board draws and which the CONTINUE path
// reaches. The screen does not care which, and must not: the reachability of a
// screen is a fact about the shell's stack, and encoding it here is how a screen
// ends up silently one-way.
class BookErrorScreen : public FocusScreen {
 public:
  // FACTS, NOT A REFERENCE -- BookDetailsScreen::Facts' precedent, and for its
  // reason: this screen is reached from the Library AND from Home's CONTINUE, and
  // only one of those has a Library to ask.
  struct Facts {
    std::string path;         // absolute on the filesystem
    std::string displayName;  // the leaf name, for the prose
    BookErrorReason reason = BookErrorReason::Damaged;
    // Where a completed delete should land. A FIELD rather than a derivation,
    // because the screen must not have to know how it was reached -- the same
    // reason Facts replaced the reference.
    ScreenId returnTo = ScreenId::Library;
  };

  explicit BookErrorScreen(Facts facts);

  ScreenId id() const override { return ScreenId::BookError; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const BookErrorViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }

  // CONSTANT, so every focus move here is a partial repaint -- DeleteConfirmScreen's
  // reasoning verbatim. Nothing this screen draws changes shape with the focus: the
  // panel is sized from the caption's wrap and the paragraph's, both fixed once the
  // screen exists, plus the slabs this shape draws -- one or two, decided at
  // construction and never after. Focus only decides which is filled and which is
  // outlined, in the same box; on the one-slab shape it cannot move at all.
  //
  // The three copy shapes have different panel heights -- they wrap to different
  // numbers of lines, and the OutOfMemory shape draws one slab where the others draw
  // two -- and that does not matter: a push is never a partial repaint
  // (App::transition() is the signal), so two instances can never be compared against
  // one frame record. The token only has to hold across focus moves within one
  // screen's life -- which is also why DeleteConfirm is constant while its caption
  // carries a book title of any length.
  uint32_t paintFootprint() const override { return 1; }

 private:
  enum Row { kOk = 0, kDelete, kRowCount };

  // HOW MANY ROWS THIS SHAPE HAS, and the ONE place reachability is decided. The
  // OutOfMemory shape draws no `DELETE FILE...` slab, so it has one row -- and
  // saying it here rather than gating `onGesture` keeps a single source of truth:
  // the focus cannot reach a row that does not exist, so there is no second
  // condition free to drift from the first. That is the class of mistake `Focus`
  // was extracted to delete.
  static int rowsFor(BookErrorReason reason);

  void syncVm() override;

  Facts facts_;
  BookErrorViewModel vm_;
};

}  // namespace reader
