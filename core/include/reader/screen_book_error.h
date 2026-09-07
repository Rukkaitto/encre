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
enum class BookErrorReason : uint8_t { Damaged, Unreadable };

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
  // screen exists, plus two kActionH slabs that are both always drawn. Focus only
  // decides which is filled and which is outlined, in the same box.
  //
  // The two copy shapes wrap to different heights, and that does not matter: a push
  // is never a partial repaint (App::transition() is the signal), so two instances
  // can never be compared against one frame record. The token only has to hold
  // across focus moves within one screen's life -- which is also why DeleteConfirm
  // is constant while its caption carries a book title of any length.
  uint32_t paintFootprint() const override { return 1; }

 private:
  enum Row { kOk = 0, kDelete, kRowCount };

  void syncVm() override;

  Facts facts_;
  BookErrorViewModel vm_;
};

}  // namespace reader
