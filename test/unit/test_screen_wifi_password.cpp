// WHAT IS LEFT OF THIS SCREEN ONCE THE KEYBOARD IS NOT IN IT. The grid, the
// caret, the modifiers, the bound's enforcement and the Back rule moved to
// test_screen_text_entry.cpp with the mechanism they belong to (#126); a
// screen's tests are about its content, and this screen's content is four
// strings, two numbers and 802.11.
//
// THE FLOOR IS THE CASE THAT EARNS ITS OWN FILE. TextEntryConfig::minLength
// defaults to 0 precisely so that a caller whose field may legally be empty
// does not inherit a dead confirm cell, and this is the one call site in the
// firmware that sets it. Both facts are asserted here rather than being left to
// the mechanism's default: the number travelling is the failure mode.
#include <string>

#include "doctest.h"
#include "reader/screen_wifi_password.h"

using namespace reader;

namespace {

const GestureEvent kConfirm{Gesture::Activate, 1, false};

// The confirm cell is the last of the function row, and it is addressed by
// POSITION rather than by the word `JOIN` -- the label is config now, so
// looking it up by name would be asserting the label using the label.
int joinCell(const WifiPasswordScreen& s) { return static_cast<int>(s.vm().cells.size()) - 1; }

void focusOn(WifiPasswordScreen& s, int at) {
  REQUIRE(at >= 0);
  s.setFocus(at);
  REQUIRE(s.focus() == at);
}

}  // namespace

TEST_CASE("the passphrase keyboard says which network it is for, and what JOIN is") {
  // FOUR STRINGS AND TWO NUMBERS, which is the whole of what this screen adds
  // to the keyboard. They are asserted because the extraction moved every one
  // of them out of the class that used to hold them, so a config that lost a
  // field would leave the band or the confirm cell blank with nothing else in
  // the suite to say so.
  WifiPasswordScreen s("PENDRAGON");
  CHECK(s.id() == ScreenId::WifiPassword);
  CHECK(s.vm().title == "PASSWORD");
  CHECK(s.vm().fieldName == "PENDRAGON");
  CHECK(s.vm().cells[static_cast<size_t>(joinCell(s))] == "JOIN");
  // SHOWN IN CLEAR, which is the board's own line. It is a Wi-Fi decision
  // about a passphrase you own rather than a property of typing, which is why
  // it is set here and asserted here.
  CHECK(s.vm().visibility == "SHOWN WHILE TYPING");
}

TEST_CASE("802.11's bound is this screen's, and setEntered clamps to it") {
  // 63 is the standard's maximum and means nothing to any other field, so the
  // mechanism takes it as a number. EDIT PASSWORD comes back through
  // setEntered and so does the factory -- both hand over a string this screen
  // did not build.
  WifiPasswordScreen s("N");
  s.setEntered(std::string(WifiPasswordScreen::kMaxPassphrase + 20, 'z'));
  CHECK(s.entered().size() == WifiPasswordScreen::kMaxPassphrase);
  CHECK(s.vm().counter == std::to_string(WifiPasswordScreen::kMaxPassphrase) + " CHARS");
}

TEST_CASE("JOIN is inert below the passphrase minimum, and the bar says so") {
  // A WPA2 passphrase is 8 to 63 characters, and this keyboard is only ever
  // reached for a LOCKED network -- an open one joins directly -- so JOIN
  // under eight cannot succeed. It would spend a radio round trip and a
  // failure dialog to report a length the counter is already showing.
  //
  // THE EMPTY SLOT IS THE EXISTING VOCABULARY, not a new one: a button with
  // no action gets an empty slot. That is what keeps this from being the
  // focuses-then-ignores defect -- the bar and the behaviour agree, and they
  // agree because one expression answers both.
  //
  // AND IT IS 802.11's ALONE. The mechanism's default is no floor, so this
  // case is the only thing in the firmware that can say the number arrived
  // where it was meant to.
  WifiPasswordScreen s("N");
  const int join = joinCell(s);

  for (size_t n = 0; n < WifiPasswordScreen::kMinPassphrase; ++n) {
    CAPTURE(n);
    s.setEntered(std::string(n, 'a'));
    focusOn(s, join);
    CHECK(s.vm().hints[1].empty());
    const Action a = s.onGesture(kConfirm);
    CHECK(a.kind == Action::Kind::None);
    CHECK_FALSE(s.joinChosen());
  }

  // AT the minimum it is live, which is the boundary that separates "8 is
  // enough" from "9 is".
  s.setEntered(std::string(WifiPasswordScreen::kMinPassphrase, 'a'));
  focusOn(s, join);
  CHECK(s.vm().hints[1] == "JOIN");
  const Action a = s.onGesture(kConfirm);
  // LATCHED, NOT POPPED, and the shell reads joinChosen() and entered() off
  // this screen while it is still standing -- a pop destroys the passphrase.
  CHECK(a.kind == Action::Kind::Wifi);
  CHECK(s.joinChosen());
}

TEST_CASE("leaving is the same latch as joining, because the radio has to come down") {
  // BOTH OUTCOMES RETURN Action::wifi(), which pops nothing: the shell tells
  // them apart by joinChosen() and cancelled(), not by the Action. That is the
  // reason TextEntryConfig carries two Actions and Wi-Fi puts the same one in
  // both -- a caller whose leave is an ordinary pop should not have to take a
  // latch with it, and this one must.
  WifiPasswordScreen s("N");
  const Action a = s.onGesture({Gesture::Back, 1, false});  // empty field, so it leaves
  CHECK(a.kind == Action::Kind::Wifi);
  CHECK(s.cancelled());
  CHECK_FALSE(s.joinChosen());
}
