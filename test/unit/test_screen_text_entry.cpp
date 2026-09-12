// THE KEYBOARD'S OWN TESTS, which is everything typing a string on six buttons
// does and nothing any one caller wants. They were test_screen_wifi_password's
// until #126 lifted the screen out from under it; what stayed there is the
// handful of facts that really are 802.11's.
//
// THE CASES ARE DRIVEN THROUGH A HARNESS AND NOT THROUGH WifiPasswordScreen,
// deliberately. Driving them through the one shipped caller is how an
// extraction quietly stops being one: every assertion would still pass against
// a base class that had the caller's title, the caller's bound and the caller's
// floor baked back into it. Here the config is the test's own, so a constant
// that crept back into the mechanism fails rather than agreeing with itself.
#include <set>
#include <string>

#include "doctest.h"
#include "reader/screen_text_entry.h"

using namespace reader;

namespace {

using Layer = TextEntryScreen::Layer;

const GestureEvent kConfirm{Gesture::Activate, 1, false};
const GestureEvent kBack{Gesture::Back, 1, false};

// A CALLER THAT IS NOT WI-FI. Its ScreenId is the ONE thing about this harness
// that carries no meaning: nothing here reaches an App, a factory or the
// session record, so no id is more correct than another and ScreenId gains no
// member for a test. Everything else -- both band slots, the confirm label, the
// visibility line, the bound, the floor and both outcome Actions -- is
// deliberately different from Wi-Fi's, so a value the mechanism still spells
// for itself shows up as the wrong string rather than as a pass.
class TestField : public TextEntryScreen {
 public:
  explicit TestField(TextEntryConfig c) : TextEntryScreen(std::move(c)) {}
  ScreenId id() const override { return ScreenId::WifiPassword; }
};

TextEntryConfig baseConfig() {
  TextEntryConfig c;
  c.title = "SIGN IN";
  c.fieldName = "EMAIL OR USERNAME";
  c.confirmLabel = "NEXT";
  c.visibility = "SHOWN WHILE TYPING";
  c.maxLength = 30;
  // minLength IS LEFT ALONE ON PURPOSE in almost every case below: the default
  // is the thing under test.
  c.confirmAction = Action::finish();
  c.leaveAction = Action::pop();
  return c;
}

// The flat index of a cell by its label. The grid is 10/10/10/10 characters
// then the six function keys, so SHIFT is 42 -- but they are looked up rather
// than spelled, because a spelled index is a second copy of the layout.
int cellNamed(const TextEntryScreen& s, const std::string& label) {
  for (size_t i = 0; i < s.vm().cells.size(); ++i)
    if (s.vm().cells[i] == label) return static_cast<int>(i);
  return -1;
}

// THE CONFIRM CELL IS THE LAST ONE, BY POSITION. It is looked up this way and
// never by its label, because its label is the CALLER's and one of the cases
// below hands it a label another key already carries.
int confirmCell(const TextEntryScreen& s) { return static_cast<int>(s.vm().cells.size()) - 1; }

// PUT THE FOCUS ON A CELL AND ASSERT IT IS THERE -- never on setFocus's
// return. That bool is "did anything MOVE", so setting the focus to where it
// already is answers false, correctly, and `REQUIRE(s.setFocus(n))` then
// fails on a screen that is doing exactly the right thing. This project has
// written that line and had to unwrite it twice; the state is what the test
// is about.
void focusOn(TextEntryScreen& s, int at) {
  REQUIRE(at >= 0);
  s.setFocus(at);
  REQUIRE(s.focus() == at);
}

// Press the cell carrying `label`, whatever layer it is on.
void press(TextEntryScreen& s, const std::string& label) {
  focusOn(s, cellNamed(s, label));
  s.onGesture(kConfirm);
}

}  // namespace

TEST_CASE("the three layers reach every printable ASCII character") {
  // The property screen_text_entry.h claims, and the reason it matters: a
  // character on no layer is a string that cannot be typed on this device at
  // all -- with nothing on the glass to say so. For Wi-Fi that is an
  // untypeable passphrase; for anything else it is an untypeable password.
  TestField s(baseConfig());
  std::set<char> reachable;
  for (const Layer l : {Layer::Lower, Layer::Upper, Layer::Symbols}) {
    s.setLayer(l);
    // EVERY LAYER IS THE SAME 46 CELLS, which is what keeps the panel
    // geometry from moving and GridFocus from meeting a changing shape. What
    // the invariant asks is that the layers AGREE, not that the number is 46.
    REQUIRE(s.vm().cells.size() == 46);
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
  // And nothing OUTSIDE it: a cell carrying a byte a password may not hold
  // would be a key that produces an unusable string.
  CHECK(reachable.size() == 95);
}

TEST_CASE("SPACE types a space, not the word") {
  // The cell's LABEL is the word and its output is one character. A cell that
  // typed its own label is the shape this screen is most exposed to, because
  // every other cell's label IS its output.
  TestField s(baseConfig());
  press(s, "SPACE");
  CHECK(s.entered() == " ");
  CHECK(s.vm().counter == "1 CHARS");
}

TEST_CASE("SHIFT is one-shot and #+= latches") {
  // A password usually needs one capital, so a shift that stayed on would
  // cost a second press to turn off far more often than it saved one; a
  // symbol page is the opposite. Both halves are asserted, because "both
  // latch" and "both are one-shot" each satisfy one of them.
  TestField s(baseConfig());
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
  // And it toggles back rather than needing another route out -- pressed by the
  // label it is SHOWING, which on this layer is `abc`. `press` looks a cell up
  // by its label, so naming `#+=` here would be looking for a cell that is not
  // on the glass: that is the whole point of the rule, expressed as the test
  // being unable to spell it the old way.
  press(s, "abc");
  CHECK(s.layer() == Layer::Lower);
}

TEST_CASE("the layer key names where it takes you, not what it is") {
  // IT LATCHES, so with one fixed label nothing on the glass distinguishes the
  // two states except the 40 cells above it -- and the key that got you to the
  // symbols would be advertising the layer you are already on. SHIFT needs no
  // such pair because it is one-shot: it is spent by the next character, so
  // there is no state for a label to name.
  //
  // Asserted at the CELL and at the HINT, because they are two readers of one
  // label and the defect this replaced was a second spelling drifting from the
  // first. The golden is the third and pins the pixels.
  TestField s(baseConfig());

  REQUIRE(s.layer() == Layer::Lower);
  REQUIRE(cellNamed(s, "#+=") >= 0);
  CHECK(cellNamed(s, "abc") == -1);

  // THE SAME CELL, not a seventh key: the row is six wide on every layer and
  // the index does not move, so a focus sitting on it survives the press.
  const int at = cellNamed(s, "#+=");
  focusOn(s, at);
  CHECK(s.vm().hints[1] == "#+=");
  s.onGesture(kConfirm);

  REQUIRE(s.layer() == Layer::Symbols);
  CHECK(s.vm().cells[static_cast<size_t>(at)] == "abc");
  CHECK(cellNamed(s, "#+=") == -1);
  CHECK(s.focus() == at);
  CHECK(s.vm().hints[1] == "abc");

  // SHIFT'S LAYER IS NOT THE SYMBOL LAYER, and this is the case that says the
  // substitution keys on the layer rather than on "not Lower": the upper layer
  // still offers the symbols, so the key still reads `#+=` there.
  s.setLayer(Layer::Upper);
  CHECK(s.vm().cells[static_cast<size_t>(at)] == "#+=");
  CHECK(s.vm().hints[1] == "#+=");

  // AND THE ROW IS OTHERWISE UNTOUCHED -- one cell varies, five do not.
  s.setLayer(Layer::Symbols);
  for (const char* f : {"SHIFT", "SPACE"}) CHECK(cellNamed(s, f) >= 0);
  CHECK(s.vm().cells[static_cast<size_t>(confirmCell(s))] == "NEXT");
  CHECK(s.vm().cells.size() == 46);
}

TEST_CASE("#+= clears a pending shift") {
  // The two modifiers are exclusive. Leaving shift armed under the symbol
  // page would make the next symbol turn it off for no visible reason -- the
  // keyboard would drop back to lowercase after a character that was never
  // shifted.
  TestField s(baseConfig());
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
  TestField s(baseConfig());
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

  // And NOW it leaves, returning the CALLER's leave Action -- Wi-Fi's is a
  // latch because the radio has to come down, and this caller's is a plain
  // pop. The mechanism has no opinion about either.
  const Action a = s.onGesture(kBack);
  CHECK(a.kind == Action::Kind::Pop);
  CHECK(s.cancelled());
}

TEST_CASE("a held Back leaves with the field full, and takes the same route out") {
  // Leaving by either route is the same event, so it sets the same flag and
  // returns the same Action -- which is what lets a caller answer one question
  // rather than two.
  TestField s(baseConfig());
  press(s, "a");
  REQUIRE_FALSE(s.entered().empty());
  const Action a = s.onGesture({Gesture::Secondary, 1, false});
  CHECK(a.kind == Action::Kind::Pop);
  CHECK(s.cancelled());
  CHECK_FALSE(s.confirmed());
}

TEST_CASE("the caret is a position, and typing inserts there") {
  // THERE WAS NO WAY TO FIX A TYPO THREE CHARACTERS BACK: every press
  // appended and Back deleted from the end, so a mistyped string had to be
  // unwound to the mistake and retyped. Reported off the device.
  TestField s(baseConfig());
  press(s, "a");
  press(s, "b");
  press(s, "d");
  REQUIRE(s.entered() == "abd");
  CHECK(s.caret() == 3);

  // Left twice, to sit between `b` and `d`.
  press(s, "\xE2\x80\xB9");
  CHECK(s.caret() == 2);
  press(s, "c");
  CHECK(s.entered() == "abcd");
  // AND THE CARET FOLLOWS WHAT WAS TYPED, so a second character lands after
  // the first rather than before it.
  CHECK(s.caret() == 3);
  press(s, "x");
  CHECK(s.entered() == "abcxd");

  // Right to the end, and past it.
  press(s, "\xE2\x80\xBA");
  CHECK(s.caret() == 5);
  const Action a = s.onGesture(kConfirm);  // still on the right arrow
  CHECK(a.kind == Action::Kind::None);  // clamped: nothing moved, nothing repaints
  CHECK(s.caret() == 5);
}

TEST_CASE("Back deletes BEFORE the caret, and does nothing at the start") {
  TestField s(baseConfig());
  s.setEntered("abcd");
  // setEntered leaves it at the end, which is where a caller coming back to
  // correct a string wants it.
  REQUIRE(s.caret() == 4);

  press(s, "\xE2\x80\xB9");
  press(s, "\xE2\x80\xB9");
  REQUIRE(s.caret() == 2);
  CHECK(s.onGesture(kBack).kind == Action::Kind::Redraw);
  CHECK(s.entered() == "acd");
  CHECK(s.caret() == 1);

  // AT THE START OF A NON-EMPTY FIELD THERE IS NOTHING BEFORE THE CARET, so
  // the press does nothing -- and in particular does NOT leave. A Back that
  // meant two different things depending on where the caret sits would be
  // worse than one that means one.
  press(s, "\xE2\x80\xB9");
  REQUIRE(s.caret() == 0);
  const Action a = s.onGesture(kBack);
  CHECK(a.kind == Action::Kind::None);
  CHECK(s.entered() == "acd");
  CHECK_FALSE(s.cancelled());
  // The bar still says DELETE, because the field is not empty -- which is
  // what that slot is keyed on.
  CHECK(s.vm().hints[0] == "DELETE");
}

TEST_CASE("the arrows are guillemets, which cost no icon and no font rebuild") {
  // `‹` and `›` are already in fontc.py's subset -- CLAUDE.md
  // records the Typography panel planning the same pair as a `make fonts`
  // pass and a flash cost that did not exist. Asserted as the CELLS' own
  // bytes so a silent swap to an icon, or to ASCII `<` and `>` (which a
  // password may legitimately contain and which the symbol layer types),
  // fails here.
  TestField s(baseConfig());
  const int left = cellNamed(s, "\xE2\x80\xB9");
  const int right = cellNamed(s, "\xE2\x80\xBA");
  REQUIRE(left >= 0);
  REQUIRE(right >= 0);
  CHECK(right == left + 1);
  // The function row is the last, and they lead it.
  CHECK(left == 40);
  CHECK(s.vm().rowWidths.back() == 6);
  // And the Confirm hint names what they do rather than repeating the glyph.
  focusOn(s, left);
  CHECK(s.vm().hints[1] == "MOVE");
}

TEST_CASE("the Back slot says which of the two things it will do") {
  // ONE EXPRESSION drives the label, the ring and the binding, so a reader
  // is never told DELETE by a button that leaves, or offered a hold that
  // does what the short press already does.
  TestField s(baseConfig());

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

  // setEntered moves it too -- a caller priming the field must not land on a
  // bar describing the empty state.
  s.setEntered("hunter2");
  CHECK(s.vm().hints[0] == "DELETE");
  CHECK(s.vm().holds[0]);
}

TEST_CASE("the bound on typing is the caller's, and a press past it does nothing") {
  // A keyboard that accepted the press and dropped the character is
  // indistinguishable from one that missed it. The NUMBER is the caller's --
  // 63 is 802.11's and means nothing to anybody else -- so this drives a bound
  // the mechanism cannot have baked in.
  TextEntryConfig c = baseConfig();
  c.maxLength = 6;
  TestField s(c);
  std::string want;
  for (size_t i = 0; i < 6; ++i) {
    press(s, "a");
    want += 'a';
  }
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

TEST_CASE("setEntered clamps to the caller's bound, because the string is not this screen's") {
  // Every caller has a door a string it did not build comes back through --
  // Wi-Fi's is EDIT PASSWORD, and the factory's priming is the other.
  TextEntryConfig c = baseConfig();
  c.maxLength = 6;
  TestField s(c);
  s.setEntered(std::string(26, 'z'));
  CHECK(s.entered().size() == 6);
  CHECK(s.vm().entered.size() == 6);
  // The counter is mirrored from the clamped value and not from the argument.
  CHECK(s.vm().counter == "6 CHARS");
}

TEST_CASE("the Confirm hint names what the focused cell does") {
  // THE THIRD HINT BAR IN THIS FIRMWARE WHOSE TEXT VARIES WITHIN A SCREEN,
  // after Settings and WifiSettings -- and the one where a fixed label was
  // outright false. It read a constant `TYPE`, so Confirm on the confirm cell
  // promised to type a character and instead left the screen.
  TestField s(baseConfig());
  s.setEntered("correcthorse");

  focusOn(s, cellNamed(s, "a"));
  CHECK(s.vm().hints[1] == "TYPE");
  focusOn(s, cellNamed(s, "SHIFT"));
  CHECK(s.vm().hints[1] == "SHIFT");
  focusOn(s, cellNamed(s, "#+="));
  CHECK(s.vm().hints[1] == "#+=");
  // THE SLOT QUOTES THE CELL rather than a literal, so on the symbol layer it
  // reads `abc` -- the label and the binding cannot disagree about which layer
  // the key leads to.
  s.setLayer(Layer::Symbols);
  CHECK(s.vm().hints[1] == "abc");
  s.setLayer(Layer::Lower);
  // SPACE IS NOT A MODIFIER: it types a character like any other cell, and it
  // is the only function key that does.
  focusOn(s, cellNamed(s, "SPACE"));
  CHECK(s.vm().hints[1] == "TYPE");
  // AND THE CONFIRM SLOT QUOTES THE CALLER'S OWN LABEL.
  focusOn(s, confirmCell(s));
  CHECK(s.vm().hints[1] == "NEXT");
  // The other three slots never move: Back, Up and Down mean the same thing
  // on every cell.
  CHECK(s.vm().hints[0] == "DELETE");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  CHECK(s.vm().holds[0]);
}

TEST_CASE("the band, the counter row and the confirm cell are all the caller's words") {
  // THE MECHANISM OWNS NO COPY EXCEPT THE NOTE. Everything a reader can read
  // on this screen that is not a key comes from the config -- which is what
  // makes a second caller a config rather than a second screen.
  TestField s(baseConfig());
  CHECK(s.vm().title == "SIGN IN");
  CHECK(s.vm().fieldName == "EMAIL OR USERNAME");
  CHECK(s.vm().visibility == "SHOWN WHILE TYPING");
  CHECK(s.vm().cells[static_cast<size_t>(confirmCell(s))] == "NEXT");
  // AND THE NOTE IS THE EXCEPTION, because it describes the input model this
  // base class owns rather than anything a caller decided: it is the same
  // sentence for every caller by construction, and a parameter would only be a
  // chance for one of them to describe a keyboard it does not have.
  CHECK(s.vm().note == "UP AND DOWN MOVE BETWEEN ROWS; THE SIDE PAGE BUTTONS MOVE ALONG A ROW.");
}

TEST_CASE("the default floor is NONE, so the confirm cell is live on an empty field") {
  // THE ONE THING THAT MAY NOT BE CARRIED OVER FROM WI-FI. A floor makes the
  // confirm cell inert and blanks the Confirm slot -- this firmware's
  // vocabulary for a button with no action -- so a floor inherited by a caller
  // whose field may legally be empty is A DEAD BUTTON ON A LEGAL STATE, which
  // this project has shipped twice and refuses.
  //
  // Instapaper's own documentation is the case that named it: "Passwords are
  // not required, and many users do not have one... you cannot treat an empty
  // password as a user error."
  //
  // ASSERTED ON THE DEFAULT-CONSTRUCTED CONFIG rather than on this test's own,
  // because a number that came back would come back as a default: a case that
  // set minLength itself would pass over it.
  CHECK(TextEntryConfig{}.minLength == 0);

  TextEntryConfig c = baseConfig();
  REQUIRE(c.minLength == 0);  // baseConfig does not touch it either
  TestField s(c);
  REQUIRE(s.entered().empty());

  // THE SLOT IS NOT BLANK, which is the half a reader sees before pressing.
  CHECK(s.confirmable());
  focusOn(s, confirmCell(s));
  CHECK(s.vm().hints[1] == "NEXT");

  // AND THE PRESS ACTS, which is the half they see after. The bar and the
  // behaviour agree because one expression (`confirmable()`) answers both.
  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::Finish);
  CHECK(s.confirmed());
  CHECK(s.entered().empty());
}

TEST_CASE("a floor is honoured when a caller sets one, and the slot goes empty below it") {
  // The other side of the default: the mechanism can still express a floor,
  // and 802.11 is the one caller that wants one. Both halves are asserted
  // because a mechanism that IGNORED minLength would also pass the case above.
  TextEntryConfig c = baseConfig();
  c.minLength = 3;
  TestField s(c);
  const int at = confirmCell(s);

  for (size_t n = 0; n < 3; ++n) {
    CAPTURE(n);
    s.setEntered(std::string(n, 'a'));
    focusOn(s, at);
    CHECK_FALSE(s.confirmable());
    CHECK(s.vm().hints[1].empty());
    CHECK(s.onGesture(kConfirm).kind == Action::Kind::None);
    CHECK_FALSE(s.confirmed());
  }

  // AT the floor it is live, which is the boundary that separates "3 is
  // enough" from "4 is".
  s.setEntered("aaa");
  focusOn(s, at);
  CHECK(s.confirmable());
  CHECK(s.vm().hints[1] == "NEXT");
  CHECK(s.onGesture(kConfirm).kind == Action::Kind::Finish);
  CHECK(s.confirmed());
}

TEST_CASE("the confirm cell is found by POSITION, so a label that collides still confirms") {
  // THE EXTRACTION IS WHAT MADE THIS REACHABLE. Every cell used to be
  // identified by its label, which was sound while all six function labels
  // were constants owned by one file. One of them is the CALLER's now, so a
  // caller naming its action `SPACE` -- or `SHIFT`, or `#+=` -- would have had
  // its confirm cell type a space or toggle a layer, and the bar would have
  // agreed with the wrong one. An index cannot collide with a string.
  TextEntryConfig c = baseConfig();
  c.confirmLabel = "SPACE";
  TestField s(c);

  // TWO CELLS NOW CARRY THE WORD, which is the state the defect needs.
  int carrying = 0;
  for (const std::string& cell : s.vm().cells)
    if (cell == "SPACE") ++carrying;
  REQUIRE(carrying == 2);

  focusOn(s, confirmCell(s));
  CHECK(s.vm().hints[1] == "SPACE");
  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::Finish);
  CHECK(s.confirmed());
  // AND IT TYPED NOTHING, which is the half that says it took the right
  // branch rather than merely latching on the way past.
  CHECK(s.entered().empty());

  // The real SPACE key is untouched and still types.
  TestField t(c);
  focusOn(t, 44);
  t.onGesture(kConfirm);
  CHECK(t.entered() == " ");
  CHECK_FALSE(t.confirmed());
}

TEST_CASE("the two outcomes are separate Actions, and a caller may make them differ") {
  // Wi-Fi hands the same latch to both, because its shell tells them apart by
  // joinChosen() and cancelled() rather than by the Action. A caller whose
  // leave is an ordinary pop should not have to take a latch with it, which is
  // why there are two fields and not one.
  TextEntryConfig c = baseConfig();
  c.confirmAction = Action::open();
  c.leaveAction = Action::pop();

  TestField confirming(c);
  focusOn(confirming, confirmCell(confirming));
  CHECK(confirming.onGesture(kConfirm).kind == Action::Kind::Open);
  CHECK(confirming.confirmed());
  CHECK_FALSE(confirming.cancelled());

  TestField leaving(c);
  CHECK(leaving.onGesture(kBack).kind == Action::Kind::Pop);
  CHECK(leaving.cancelled());
  CHECK_FALSE(leaving.confirmed());
}
