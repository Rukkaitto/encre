// THE FILE screen_wifi_password.h HAS BEEN CITING SINCE IT WAS WRITTEN, and
// which did not exist. Its line 17 says the all-95-printable-ASCII property is
// "asserted rather than assumed, in test_screen_wifi_password.cpp".
//
// The property was true -- but nothing in the suite asserted it, or anything
// else this screen DOES. An adversarial mutation pass put ten mutations
// through it and every one survived: typing past the bound, setEntered's
// clamp, Back-on-empty popping instead of deleting, SHIFT latching instead of
// one-shot, #+= not clearing a pending shift, SPACE typing the literal word
// "SPACE". What was defended was the PIXELS -- dropping `~` from the symbol
// layer reddens wifi_password_symbols -- which is the pattern that pass found
// across the whole flow: everything the goldens draw is defended, almost
// nothing the screens do.
#include <set>
#include <string>

#include "doctest.h"
#include "reader/screen_wifi_password.h"

using namespace reader;

namespace {

using Layer = WifiPasswordScreen::Layer;

const GestureEvent kConfirm{Gesture::Activate, 1, false};
const GestureEvent kBack{Gesture::Back, 1, false};

// The flat index of a cell by its label. The grid is 10/10/10/10 characters
// then the four function keys, so SHIFT is 40 and JOIN is 43 -- but they are
// looked up rather than spelled, because a spelled index is a second copy of
// the layout.
int cellNamed(const WifiPasswordScreen& s, const std::string& label) {
  for (size_t i = 0; i < s.vm().cells.size(); ++i)
    if (s.vm().cells[i] == label) return static_cast<int>(i);
  return -1;
}

// PUT THE FOCUS ON A CELL AND ASSERT IT IS THERE -- never on setFocus's
// return. That bool is "did anything MOVE", so setting the focus to where it
// already is answers false, correctly, and `REQUIRE(s.setFocus(n))` then
// fails on a screen that is doing exactly the right thing. This project has
// written that line and had to unwrite it twice; the state is what the test
// is about.
void focusOn(WifiPasswordScreen& s, int at) {
  REQUIRE(at >= 0);
  s.setFocus(at);
  REQUIRE(s.focus() == at);
}

// Press the cell carrying `label`, whatever layer it is on.
void press(WifiPasswordScreen& s, const std::string& label) {
  focusOn(s, cellNamed(s, label));
  s.onGesture(kConfirm);
}

}  // namespace

TEST_CASE("the three layers reach every printable ASCII character") {
  // THE HEADER'S OWN CLAIM, and the reason it matters: a WPA2 passphrase may
  // contain any of them, so a character missing from all three layers is a
  // password that cannot be typed on this device at all -- with nothing on
  // the glass to say so.
  WifiPasswordScreen s("N");
  std::set<char> reachable;
  for (const Layer l : {Layer::Lower, Layer::Upper, Layer::Symbols}) {
    s.setLayer(l);
    // EVERY LAYER IS THE SAME 44 CELLS, which is what keeps the panel
    // geometry from moving and GridFocus from meeting a changing shape.
    REQUIRE(s.vm().cells.size() == 44);
    for (size_t i = 0; i < 40; ++i) {
      REQUIRE(s.vm().cells[i].size() == 1);
      reachable.insert(s.vm().cells[i][0]);
    }
  }
  // SPACE is the 95th and it is a function key, not a cell -- which is the
  // only reason 40 x 3 cells with repeats can cover 95 characters at all.
  reachable.insert(' ');

  std::set<char> printable;
  for (char c = 0x20; c > 0 && c <= 0x7E; ++c) printable.insert(c);
  REQUIRE(printable.size() == 95);
  for (const char c : printable) {
    CAPTURE(static_cast<int>(c));
    CHECK(reachable.count(c) == 1);
  }
  // And nothing OUTSIDE it: a cell carrying a byte a passphrase may not hold
  // would be a key that produces an unusable password.
  CHECK(reachable.size() == 95);
}

TEST_CASE("SPACE types a space, not the word") {
  // The cell's LABEL is the word and its output is one character. A cell that
  // typed its own label is the shape this screen is most exposed to, because
  // every other cell's label IS its output.
  WifiPasswordScreen s("N");
  press(s, "SPACE");
  CHECK(s.entered() == " ");
  CHECK(s.vm().counter == "1 CHARS");
}

TEST_CASE("SHIFT is one-shot and #+= latches") {
  // A passphrase usually needs one capital, so a shift that stayed on would
  // cost a second press to turn off far more often than it saved one; a
  // symbol page is the opposite. Both halves are asserted, because "both
  // latch" and "both are one-shot" each satisfy one of them.
  WifiPasswordScreen s("N");
  CHECK(s.layer() == Layer::Lower);

  press(s, "SHIFT");
  CHECK(s.layer() == Layer::Upper);
  press(s, "A");
  CHECK(s.entered() == "A");
  // SPENT BY THE CHARACTER, which is the one-shot.
  CHECK(s.layer() == Layer::Lower);
  press(s, "b");
  CHECK(s.entered() == "Ab");

  press(s, "#+=");
  CHECK(s.layer() == Layer::Symbols);
  press(s, "~");
  CHECK(s.entered() == "Ab~");
  // STILL THERE, which is the latch.
  CHECK(s.layer() == Layer::Symbols);
  // `@` rather than `!`: `!` is on the BASE layer's tail and not on the
  // symbol page at all, so pressing it here would be looking for a cell that
  // is not showing. The two tails are disjoint apart from `.` and `-`.
  press(s, "@");
  CHECK(s.entered() == "Ab~@");
  CHECK(s.layer() == Layer::Symbols);
  // And it toggles back rather than needing another route out.
  press(s, "#+=");
  CHECK(s.layer() == Layer::Lower);
}

TEST_CASE("#+= clears a pending shift") {
  // The two modifiers are exclusive. Leaving shift armed under the symbol
  // page would make the next symbol turn it off for no visible reason -- the
  // keyboard would drop back to lowercase after a character that was never
  // shifted.
  WifiPasswordScreen s("N");
  press(s, "SHIFT");
  REQUIRE(s.layer() == Layer::Upper);
  press(s, "#+=");
  REQUIRE(s.layer() == Layer::Symbols);
  press(s, "@");
  CHECK(s.entered() == "@");
  // The shift is GONE rather than spent here: a spent shift would have
  // dropped the layer to Lower.
  CHECK(s.layer() == Layer::Symbols);
}

TEST_CASE("Back deletes a character, and LEAVES when there is none to delete") {
  // THIS CASE USED TO ASSERT THE DEFECT. It said "on an empty field it does
  // nothing at all", on the reasoning that the bar says DELETE and the hold
  // is the way out -- and that is a dead button plus a hold as the only exit
  // from a screen a reader can arrive at by accident. Reported off the
  // device as "you can't go back from the password screen".
  //
  // A character in the field is still a delete, and the last one is still a
  // delete rather than an exit: the press that empties the field does not
  // also leave.
  WifiPasswordScreen s("N");
  press(s, "a");
  press(s, "b");
  REQUIRE(s.entered() == "ab");

  CHECK(s.onGesture(kBack).kind == Action::Kind::Redraw);
  CHECK(s.entered() == "a");
  CHECK_FALSE(s.cancelled());
  CHECK(s.onGesture(kBack).kind == Action::Kind::Redraw);
  CHECK(s.entered().empty());
  // THE PRESS THAT EMPTIED IT DID NOT ALSO LEAVE, which is the boundary: a
  // rule keyed on "after this press the field is empty" would take the
  // reader off the screen on the last backspace.
  CHECK_FALSE(s.cancelled());

  // And NOW it leaves, latching rather than popping, because the shell has
  // to take the radio down.
  const Action a = s.onGesture(kBack);
  CHECK(a.kind == Action::Kind::Wifi);
  CHECK(s.cancelled());
}

TEST_CASE("the Back slot says which of the two things it will do") {
  // ONE EXPRESSION drives the label, the ring and the binding, so a reader
  // is never told DELETE by a button that leaves, or offered a hold that
  // does what the short press already does.
  WifiPasswordScreen s("N");

  // Empty: the short press leaves, so the slot says BACK and there is NO
  // ring -- a ring promises a DIFFERENT action and there is no second one.
  CHECK(s.vm().hints[0] == "BACK");
  CHECK_FALSE(s.vm().holds[0]);
  CHECK(s.longPressable() == 0);

  press(s, "a");
  CHECK(s.vm().hints[0] == "DELETE");
  CHECK(s.vm().holds[0]);
  CHECK(s.longPressable() != 0);

  // ...and back again, because the field can empty.
  s.onGesture(kBack);
  REQUIRE(s.entered().empty());
  CHECK(s.vm().hints[0] == "BACK");
  CHECK_FALSE(s.vm().holds[0]);

  // setEntered moves it too -- the EDIT PASSWORD path arrives through there
  // and must not land on a bar describing the empty state.
  s.setEntered("hunter2");
  CHECK(s.vm().hints[0] == "DELETE");
  CHECK(s.vm().holds[0]);
}

TEST_CASE("typing stops at the bound rather than swallowing the press") {
  // 802.11's own maximum. A keyboard that accepted the press and dropped the
  // character is indistinguishable from one that missed it.
  WifiPasswordScreen s("N");
  std::string want;
  for (size_t i = 0; i < WifiPasswordScreen::kMaxPassphrase; ++i) {
    press(s, "a");
    want += 'a';
  }
  REQUIRE(s.entered().size() == WifiPasswordScreen::kMaxPassphrase);
  REQUIRE(s.entered() == want);

  const int at = cellNamed(s, "b");
  focusOn(s, at);
  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::None);
  CHECK(s.entered() == want);

  // SPACE is two bytes of nothing here for the same reason -- the guard is on
  // the text's length, not on the cell being a single character.
  press(s, "SPACE");
  CHECK(s.entered() == want);
}

TEST_CASE("setEntered clamps, because a record from elsewhere is not this screen's to trust") {
  // EDIT PASSWORD comes back through here, and so does the factory. Both hand
  // over a string this screen did not build.
  WifiPasswordScreen s("N");
  s.setEntered(std::string(WifiPasswordScreen::kMaxPassphrase + 20, 'z'));
  CHECK(s.entered().size() == WifiPasswordScreen::kMaxPassphrase);
  CHECK(s.vm().entered.size() == WifiPasswordScreen::kMaxPassphrase);
  // The counter is mirrored from the clamped value and not from the argument.
  CHECK(s.vm().counter == std::to_string(WifiPasswordScreen::kMaxPassphrase) + " CHARS");
}

TEST_CASE("the Confirm hint names what the focused cell does") {
  // THE THIRD HINT BAR IN THIS FIRMWARE WHOSE TEXT VARIES WITHIN A SCREEN,
  // after Settings and WifiSettings -- and the one where a fixed label was
  // outright false. It read a constant `TYPE`, so Confirm on JOIN promised to
  // type a character and instead left the screen and started a join.
  WifiPasswordScreen s("N");
  s.setEntered("correcthorse");

  focusOn(s, cellNamed(s, "a"));
  CHECK(s.vm().hints[1] == "TYPE");
  focusOn(s, cellNamed(s, "SHIFT"));
  CHECK(s.vm().hints[1] == "SHIFT");
  focusOn(s, cellNamed(s, "#+="));
  CHECK(s.vm().hints[1] == "#+=");
  // SPACE IS NOT A MODIFIER: it types a character like any other cell, and it
  // is the only function key that does.
  focusOn(s, cellNamed(s, "SPACE"));
  CHECK(s.vm().hints[1] == "TYPE");
  focusOn(s, cellNamed(s, "JOIN"));
  CHECK(s.vm().hints[1] == "JOIN");
  // The other three slots never move: Back, Up and Down mean the same thing
  // on every cell.
  CHECK(s.vm().hints[0] == "DELETE");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  CHECK(s.vm().holds[0]);
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
  WifiPasswordScreen s("N");
  const int join = cellNamed(s, "JOIN");
  REQUIRE(join >= 0);

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
  CHECK(a.kind == Action::Kind::Wifi);
  CHECK(s.joinChosen());
}
