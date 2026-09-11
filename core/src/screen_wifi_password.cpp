#include "reader/screen_wifi_password.h"

#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

// THE BOARD'S OWN BASE LAYER: a-j / k-t / u-z 0 1 2 3 / 4-9 . - _ !
//
// A `kLower` HOLDING THAT WHOLE STRING USED TO SIT HERE AND NOTHING READ IT.
// rebuildCells builds its own, and the dead copy was 34 characters against
// the 40 the layer actually has -- a second, WRONG spelling of the layout,
// under this comment claiming to be the board's. Nothing could catch it: an
// unread constant renders nothing, so the goldens agreed with the real
// string while the file documented the other one.
//
// 26 letters + 0-3 on row three, then 4-9 and four punctuation on row four.
// The tail is written out rather than generated so the board and the code can
// be read side by side; the letters are generated, because a second literal
// alphabet is what produced the defect above.
constexpr const char* kBaseTail = "456789.-_!";

// The 28 punctuation characters the base layer does NOT carry, then the two
// most useful repeats, over a row of digits. 10 + 30 = 40.
//
// THE UNION OF THE THREE LAYERS IS ALL 95 PRINTABLE ASCII, which is what a
// WPA2 passphrase may contain -- asserted in the test rather than counted by
// eye here.
constexpr const char* kSymbolTail = "\"#$%&'()*+,/:;<=>?@[\\]^`{|}~.-";

// SIX CELLS, the two new ones being the caret's. `\u2039` and `\u203A` rather
// than an icon pair: both are already in fontc.py's subset, so they cost no
// mark, no font rebuild and no flash -- CLAUDE.md records the Typography
// panel reaching the same conclusion about the same two codepoints.
constexpr const char* kCaretLeft = "\xE2\x80\xB9";
constexpr const char* kCaretRight = "\xE2\x80\xBA";
const char* kFunctionRow[6] = {kCaretLeft, kCaretRight, "SHIFT", "#+=", "SPACE", "JOIN"};

std::string upperOf(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

}  // namespace

WifiPasswordScreen::WifiPasswordScreen(std::string ssid)
    : GridFocusScreen(GridFocus({10, 10, 10, 10, 6})), ssid_(std::move(ssid)) {
  // The side buttons move along a row and the front buttons change row --
  // the board's own note, and a mechanism ReaderScreen and PeekScreen already
  // use rather than one this screen invents.
  declareSplitMovers();
  rebuildCells();
  syncVm();
}

void WifiPasswordScreen::rebuildCells() {
  std::string chars;
  switch (layer_) {
    case Layer::Lower:
      chars = std::string("abcdefghijklmnopqrstuvwxyz0123") + kBaseTail;
      break;
    case Layer::Upper:
      chars = upperOf("abcdefghijklmnopqrstuvwxyz") + "0123" + kBaseTail;
      break;
    case Layer::Symbols:
      chars = std::string("0123456789") + kSymbolTail;
      break;
  }
  vm_.cells.clear();
  vm_.cells.reserve(46);
  for (const char c : chars) vm_.cells.push_back(std::string(1, c));
  for (const char* f : kFunctionRow) vm_.cells.push_back(f);
  vm_.rowWidths = {10, 10, 10, 10, 6};
}

void WifiPasswordScreen::setLayer(Layer l) {
  if (layer_ == l) return;
  layer_ = l;
  rebuildCells();
  syncVm();
}

void WifiPasswordScreen::setEntered(std::string text) {
  if (text.size() > kMaxPassphrase) text.resize(kMaxPassphrase);
  entered_ = std::move(text);
  // AT THE END, which is where somebody arriving from EDIT PASSWORD wants it:
  // the usual reason to come back is that the last characters were wrong.
  caret_ = entered_.size();
  syncVm();
}

bool WifiPasswordScreen::moveCaret(int delta) {
  const int at = static_cast<int>(caret_) + delta;
  const int clamped = at < 0 ? 0 : (at > static_cast<int>(entered_.size())
                                        ? static_cast<int>(entered_.size())
                                        : at);
  // CLAMPS RATHER THAN WRAPPING, which is Focus::set's rule and for its
  // reason: a caret that jumped from the start of a passphrase to its end
  // would be indistinguishable from a misread press.
  if (static_cast<size_t>(clamped) == caret_) return false;
  caret_ = static_cast<size_t>(clamped);
  syncVm();
  return true;
}

void WifiPasswordScreen::syncVm() {
  vm_.title = "PASSWORD";
  vm_.ssid = ssid_;
  // SHOWN IN CLEAR, which is the board's `SHOWN WHILE TYPING`: at one
  // character per ~520 ms repaint on a 44-cell grid, a typo you cannot see is
  // punishing, and this is a device you hold.
  vm_.entered = entered_;
  vm_.caret = caret_;
  vm_.counter = std::to_string(entered_.size()) + " CHARS";
  vm_.visibility = "SHOWN WHILE TYPING";
  vm_.note = "UP AND DOWN MOVE BETWEEN ROWS; THE SIDE PAGE BUTTONS MOVE ALONG A ROW.";
  vm_.focusedCell = focus();
  // BACK DELETES AND A HELD BACK LEAVES -- spec 4.1b, and the ring the board
  // draws. It is the only way off this screen, which is why the hold is bound
  // rather than merely drawn.
  //
  // THE CONFIRM LABEL NAMES WHAT THE FOCUSED CELL DOES, and says nothing when
  // the cell does nothing. It was a constant `TYPE`, which is false on three
  // of the four function keys and outright misleading on JOIN -- a Confirm
  // labelled TYPE that leaves the screen and starts a join. Settings is the
  // precedent for a label that varies within a screen and WifiSettings is the
  // second; the board carries the rule.
  //
  // The EMPTY slot on an unusable JOIN is the existing vocabulary rather than
  // a new one: a button with no action gets an empty slot, drawn at
  // kHintEmptySlotW rather than as nothing. activateCell asks `joinable()`
  // too, so the bar and the behaviour cannot disagree.
  // BACK DELETES A CHARACTER AND LEAVES WHEN THERE IS NONE, and the slot says
  // which. It read a constant DELETE with a constant ring, so on an empty
  // field the press did nothing at all -- reported off the device as "you
  // can't go back from the password screen", which is exactly what it was:
  // the hold was the only way out of a screen a reader can arrive at by
  // accident.
  //
  // THE RING GOES WITH IT. A hold ring promises a DIFFERENT action, and once
  // the short press already leaves there is no second action to promise. One
  // expression drives the label, the ring and the binding, so none of the
  // three can drift from the others.
  const bool deletes = !entered_.empty();
  vm_.hints = {deletes ? "DELETE" : "BACK", confirmLabel(), "UP", "DOWN"};
  vm_.holds = {deletes, false, false, false};
  declareHints(vm_.holds);
}

std::string WifiPasswordScreen::confirmLabel() const {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(vm_.cells.size())) return "TYPE";
  const std::string& cell = vm_.cells[static_cast<size_t>(f)];
  // SPACE is not in this list, and that is the distinction: it types a
  // character like any other cell and is the only function key that does.
  if (cell == kCaretLeft || cell == kCaretRight) return "MOVE";
  if (cell == "SHIFT" || cell == "#+=") return cell;
  if (cell == "JOIN") return joinable() ? "JOIN" : "";
  return "TYPE";
}

Action WifiPasswordScreen::activateCell() {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(vm_.cells.size())) return Action::none();
  const std::string& cell = vm_.cells[static_cast<size_t>(f)];

  if (cell == "SHIFT") {
    // One-shot: armed here, spent by the next character.
    shiftArmed_ = !shiftArmed_;
    setLayer(shiftArmed_ ? Layer::Upper : Layer::Lower);
    return Action::redraw();
  }
  if (cell == "#+=") {
    // Latching, and it clears a pending shift: the two modifiers are
    // exclusive, so leaving shift armed under the symbol page would make the
    // next symbol turn it off for no visible reason.
    shiftArmed_ = false;
    setLayer(layer_ == Layer::Symbols ? Layer::Lower : Layer::Symbols);
    return Action::redraw();
  }
  if (cell == kCaretLeft) return moveCaret(-1) ? Action::redraw() : Action::none();
  if (cell == kCaretRight) return moveCaret(+1) ? Action::redraw() : Action::none();
  if (cell == "JOIN") {
    // TOO SHORT TO BE A PASSPHRASE, so the press does nothing -- and the bar
    // has already said so with an empty Confirm slot, which is what keeps
    // this from being the focuses-then-ignores defect. One spelling
    // (`joinable()`) answers both.
    if (!joinable()) return Action::none();
    join_ = true;
    // LATCHED, NOT POPPED. The shell reads joinChosen() and entered() off this
    // screen while it is still standing and then decides where the flow goes
    // -- a pop would destroy the object holding the passphrase. See
    // Action::wifi().
    return Action::wifi();
  }

  const std::string text = (cell == "SPACE") ? " " : cell;
  // TYPING STOPS AT THE BOUND rather than silently dropping the keypress: a
  // keyboard that swallows a press is indistinguishable from one that missed
  // it.
  if (entered_.size() + text.size() > kMaxPassphrase) return Action::none();
  // INSERTED AT THE CARET, not appended. With the caret at the end -- which
  // is where it starts and where it spends most of its life -- this is the
  // same thing.
  entered_.insert(caret_, text);
  caret_ += text.size();
  if (shiftArmed_) {
    shiftArmed_ = false;
    setLayer(Layer::Lower);
  }
  syncVm();
  return Action::redraw();
}

Action WifiPasswordScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      // DELETE while there is something to delete, and LEAVE when there is
      // not -- which is what the bar says in each state. This returned
      // none() on an empty field, so the press was a dead button and the
      // hold was the only way off the screen.
      //
      // IT LATCHES RATHER THAN POPPING, exactly as the hold does: the radio
      // was brought up for this join and the shell has to be told. Leaving
      // by either route is the same event, so it sets the same flag.
      if (entered_.empty()) {
        cancelled_ = true;
        return Action::wifi();
      }
      // BEFORE THE CARET, which is what backspace means -- and at the start of
      // a non-empty field there is nothing before it, so the press does
      // nothing rather than deleting the character after it or leaving the
      // screen. Leaving there would be a Back that means two different things
      // depending on where the caret sits.
      if (caret_ == 0) return Action::none();
      entered_.erase(caret_ - 1, 1);
      --caret_;
      syncVm();
      return Action::redraw();
    case Gesture::Secondary:
      // The held Back, which is the only way off this screen. It latches
      // rather than popping because the shell has work to do -- the radio was
      // brought up for this join and nothing else is going to take it down --
      // and because cancelled() is unreadable from a destroyed screen.
      cancelled_ = true;
      return Action::wifi();
    case Gesture::Activate:
      return activateCell();
    // WITH SPLIT MOVERS, the front buttons arrive as AltPrev/AltNext and the
    // side buttons as Prev/Next. Rows and columns respectively, which is the
    // board's note and not a mapping this screen invented.
    case Gesture::AltPrev:
      return moveRow(-g.steps, g.held);
    case Gesture::AltNext:
      return moveRow(+g.steps, g.held);
    case Gesture::Prev:
      return moveCol(-g.steps, g.held);
    case Gesture::Next:
      return moveCol(+g.steps, g.held);
    default:
      return Action::none();
  }
}

void WifiPasswordScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                Plane plane) const {
  theme.renderWifiPassword(fb, fonts, vm_, plane);
}

}  // namespace reader
