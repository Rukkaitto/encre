// design/BookError.dc.html and design/BookErrorUnreadable.dc.html -- one screen,
// two copy shapes.
#include "doctest.h"
#include "reader/screen_book_error.h"

using reader::Action;
using reader::BookErrorReason;
using reader::BookErrorScreen;
using reader::Gesture;
using reader::GestureEvent;
using reader::ScreenId;

namespace {
BookErrorScreen::Facts damaged() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Damaged,
          ScreenId::Library};
}
BookErrorScreen::Facts unreadable() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Unreadable,
          ScreenId::Home};
}
}  // namespace

TEST_CASE("the dialog is an overlay over whatever asked to open the book") {
  BookErrorScreen s(damaged());
  CHECK(s.id() == ScreenId::BookError);
  CHECK(s.isOverlay());
  // Chrome, one waveform. The parent may be the Reader-less Library or Home; neither
  // is grayscale, and this screen has no continuous tone of its own.
  CHECK(s.fidelity() == reader::Fidelity::Mono);
}

TEST_CASE("the damaged shape names the file and says it was left alone") {
  BookErrorScreen s(damaged());
  CHECK(s.vm().title == "CAN\xE2\x80\x99T OPEN FILE");
  // NAMES THE FILE. A dialog that does not name the thing is one people learn to
  // dismiss without reading -- DeleteConfirmScreen's own stated reason.
  CHECK(s.vm().message.find("dubliners.epub") != std::string::npos);
  CHECK(s.vm().message.find("damaged") != std::string::npos);
  CHECK(s.vm().okLabel == "OK");
  CHECK(s.vm().deleteLabel == "DELETE FILE\xE2\x80\xA6");
}

TEST_CASE("the unreadable shape does not claim the book is damaged") {
  BookErrorScreen s(unreadable());
  CHECK(s.vm().message.find("dubliners.epub") != std::string::npos);
  // THE WHOLE POINT OF THE SECOND SHAPE. openRead returning null is a file that is
  // gone or a card that is, and pollCardPresence takes 2-25s to notice -- so this
  // must not say `damaged` for a book that is perfectly fine.
  CHECK(s.vm().message.find("damaged") == std::string::npos);
  CHECK(s.vm().message.find("could not be read") != std::string::npos);
}

TEST_CASE("the board's four hint slots, and no hold") {
  BookErrorScreen s(damaged());
  CHECK(s.vm().hints[0] == "CLOSE");
  CHECK(s.vm().hints[1] == "SELECT");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  for (const bool h : s.vm().holds) CHECK_FALSE(h);
}

TEST_CASE("focus starts on OK, so a press before reading dismisses") {
  BookErrorScreen s(damaged());
  CHECK(s.focus() == 0);
  CHECK(s.vm().focusedAction == 0);
}

TEST_CASE("Back is close, and so is OK") {
  BookErrorScreen s(damaged());
  CHECK(s.onGesture({Gesture::Back}).kind == Action::Kind::Pop);
  CHECK(s.onGesture({Gesture::Activate}).kind == Action::Kind::Pop);
}

TEST_CASE("DELETE FILE... opens the confirmation") {
  BookErrorScreen s(damaged());
  REQUIRE(s.onGesture({Gesture::Next}).kind != Action::Kind::None);
  REQUIRE(s.focus() == 1);
  const Action a = s.onGesture({Gesture::Activate});
  // REPLACE, not Push. Both are overlays and App::render draws every overlay above
  // the topmost non-overlay, so a push left THIS panel standing under the
  // confirmation's veil -- and this panel is TALLER than the confirmation, so it
  // stood out above and below rather than being covered the way the actions panel
  // is. Reported off the device.
  CHECK(a.kind == Action::Kind::Replace);
  CHECK(a.target == ScreenId::DeleteConfirm);
}

TEST_CASE("the delete slab is reachable on BOTH shapes") {
  // Making it inert on the unreadable shape was considered and rejected: the two
  // shapes differ only by a sentence of prose, so a reader meeting an inert slab
  // has nothing to learn the rule from. That is the `works only sometimes` trap.
  BookErrorScreen s(unreadable());
  REQUIRE(s.onGesture({Gesture::Next}).kind != Action::Kind::None);
  CHECK(s.focus() == 1);
  CHECK(s.onGesture({Gesture::Activate}).target == ScreenId::DeleteConfirm);
}

TEST_CASE("the focus wraps, as every list here does") {
  BookErrorScreen s(damaged());
  s.onGesture({Gesture::Prev});
  CHECK(s.focus() == 1);
}

TEST_CASE("the facts carry where to return to after a delete") {
  CHECK(BookErrorScreen(damaged()).facts().returnTo == ScreenId::Library);
  // The whole reason DeleteConfirm takes Facts: Home's CONTINUE has no Library.
  CHECK(BookErrorScreen(unreadable()).facts().returnTo == ScreenId::Home);
}
