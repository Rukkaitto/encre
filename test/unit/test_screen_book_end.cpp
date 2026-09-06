// THE END OF A BOOK (design/BookEnd.dc.html), as behaviour rather than pixels.
//
// It is built directly rather than through the factory or an App, which is
// BookDetailsScreen's own shape and for its reason: this screen takes FACTS, so a
// fixture that reached for a Library or a Reader to make one would be asserting the
// coupling the Facts struct exists to remove.
#include "doctest.h"
#include "reader/screen_book_end.h"

namespace {

reader::BookEndScreen::Facts middlemarch(bool libraryBeneath = true) {
  reader::BookEndScreen::Facts f;
  f.bookTitle = "Middlemarch";
  f.author = "George Eliot";
  f.chapterCount = 24;
  f.libraryBeneath = libraryBeneath;
  return f;
}

reader::GestureEvent press(reader::Gesture g) {
  reader::GestureEvent e;
  e.what = g;
  return e;
}

}  // namespace

// THE BOARD'S OWN CONTENT, composed by the screen rather than the theme -- a byline
// and a count are CONTENT, and the theme has no business knowing a book has an author.
TEST_CASE("BookEnd states the board's lines") {
  reader::BookEndScreen s(middlemarch());
  // NO BAND VALUE. The band's right slot held the shouted book name and now holds
  // nothing: a long title squeezed `BOOK FINISHED` until the LABEL elided, and the
  // slot said what the byline below already says.
  CHECK(s.vm().title == "THE END");
  CHECK(s.vm().byline == "Middlemarch \xC2\xB7 George Eliot");
  CHECK(s.vm().meta == "24 CHAPTERS");
}

// THE LABEL FOLLOWS THE STACK. popTo(Library) stops at the root when no Library is
// on it, so the button always works and only its NAME could be wrong -- which is the
// `About this book` shape: correct in the common case and quietly wrong otherwise.
TEST_CASE("the leaving slab names where it actually lands") {
  CHECK(reader::BookEndScreen(middlemarch(true)).vm().leaveLabel == "BACK TO LIBRARY");
  CHECK(reader::BookEndScreen(middlemarch(false)).vm().leaveLabel == "BACK TO HOME");
}

// A book with no author must not produce a byline ending in a dangling separator:
// that reads as a field that failed to load rather than as a book that did not say.
TEST_CASE("a book with no author drops the separator, not just the name") {
  reader::BookEndScreen::Facts f = middlemarch();
  f.author.clear();
  reader::BookEndScreen s(f);
  CHECK(s.vm().byline == "Middlemarch");
}

// A book whose chapter count is unknown states nothing rather than `0 CHAPTERS` --
// the same call homeVmForCard makes between "no books" and "could not look".
TEST_CASE("an unknown chapter count draws no meta line") {
  reader::BookEndScreen::Facts f = middlemarch();
  f.chapterCount = 0;
  CHECK(reader::BookEndScreen(f).vm().meta.empty());
}

TEST_CASE("the focus starts on MARK AS FINISHED and wraps between the two slabs") {
  reader::BookEndScreen s(middlemarch());
  CHECK(s.focus() == 0);
  CHECK(s.onGesture(press(reader::Gesture::Next)).kind == reader::Action::Kind::Redraw);
  CHECK(s.focus() == 1);
  // EVERY LIST HERE WRAPS. On a two-row screen a wrap is unambiguous, which is the
  // case CLAUDE.md already calls settled for a four-row overlay.
  s.onGesture(press(reader::Gesture::Next));
  CHECK(s.focus() == 0);
  // And the mirror the theme reads follows it, which is the whole of what a screen
  // still does about focus.
  CHECK(s.vm().focusedAction == 0);
}

TEST_CASE("the slabs do what the board promises") {
  reader::BookEndScreen s(middlemarch());
  CHECK(s.onGesture(press(reader::Gesture::Activate)).kind == reader::Action::Kind::Finish);

  s.onGesture(press(reader::Gesture::Next));
  const reader::Action leave = s.onGesture(press(reader::Gesture::Activate));
  CHECK(leave.kind == reader::Action::Kind::PopTo);
  CHECK(leave.target == reader::ScreenId::Library);
}

// BACK RETURNS TO THE LAST PAGE, which is the whole reason this screen is PUSHED
// over the Reader rather than swapped in for it.
TEST_CASE("Back pops to the page the reader just finished") {
  reader::BookEndScreen s(middlemarch());
  CHECK(s.onGesture(press(reader::Gesture::Back)).kind == reader::Action::Kind::Pop);
}

// The bar promises four buttons and no holds, so no slot may show a ring.
TEST_CASE("the hint bar states the board's four labels and binds no hold") {
  reader::BookEndScreen s(middlemarch());
  CHECK(s.vm().hints[0] == "BACK");
  CHECK(s.vm().hints[1] == "SELECT");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  CHECK(s.longPressable() == 0);
}

// NOT AN OVERLAY. The board is a whole screen -- its own header band, its own hint
// bar, no veil and no panel -- so App::render must clear and paint this alone rather
// than paint the Reader underneath it first.
TEST_CASE("the end of a book is a whole screen, not a panel over the page") {
  reader::BookEndScreen s(middlemarch());
  CHECK_FALSE(s.isOverlay());
  CHECK(s.id() == reader::ScreenId::BookEnd);
}
