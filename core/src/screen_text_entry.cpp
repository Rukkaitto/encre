#include "reader/screen_text_entry.h"

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
// password field may contain -- asserted in the test rather than counted by
// eye here.
constexpr const char* kSymbolTail = "\"#$%&'()*+,/:;<=>?@[\\]^`{|}~.-";

// SIX CELLS, the two new ones being the caret's. `‹` and `›` rather
// than an icon pair: both are already in fontc.py's subset, so they cost no
// mark, no font rebuild and no flash -- CLAUDE.md records the Typography
// panel reaching the same conclusion about the same two codepoints.
constexpr const char* kCaretLeft = "\xE2\x80\xB9";
constexpr const char* kCaretRight = "\xE2\x80\xBA";

// THE LAYER KEY NAMES WHERE IT TAKES YOU, NOT WHAT IT IS. It LATCHES, so with
// one fixed label nothing on the glass distinguishes the two states except the
// 40 cells above it -- and the key that got you to the symbols would be sitting
// there advertising the layer you are already on. SHIFT needs no such pair,
// because it is one-shot: it is spent by the next character, so there is no
// state for a label to name.
//
// Lowercase where every other word on this row is caps, because both spellings
// are SPECIMENS of the layer they open rather than words for it, and the base
// layer's letters really are lowercase. `ABC` would name a layer SHIFT reaches
// and this key does not. design/WifiPassword.dc.html carries both halves.
//
// IT COSTS NO GEOMETRY: at Meta500/0.1em `abc` measures 44px against `#+=`'s
// 45, so it is a pixel NARROWER inside the same 60px cell that theme_quiet's
// kKeyFnW pins. The firmware is the only engine that ever draws it -- the
// symbol layer is not a board -- and `wifi_password_symbols` is what pins it.
constexpr const char* kSymbolKey = "#+=";
constexpr const char* kBaseKey = "abc";

constexpr const char* kShiftKey = "SHIFT";
constexpr const char* kSpaceKey = "SPACE";

// THE FUNCTION ROW IS ADDRESSED BY INDEX, NOT BY LABEL, AND THE EXTRACTION IS
// WHAT FORCED THAT. This screen used to identify every cell by its label,
// which was sound while all six labels were constants owned by this file. One
// of them is the CALLER'S now -- `JOIN` for Wi-Fi and something else for
// whoever is next -- so a caller passing `SPACE`, `SHIFT` or `#+=` as its
// confirm label would have had its confirm cell type a space or toggle a
// layer. An index cannot collide with a string.
//
// The character cells stay label-driven, because a character cell's label IS
// its output; only the six that mean something else need naming.
constexpr int kCharCells = 40;
enum Fn { kFnCaretLeft = 0, kFnCaretRight, kFnShift, kFnLayer, kFnSpace, kFnConfirm, kFnCount };

// Which function key a flat cell index is, or -1 for a character cell. One
// spelling, asked by confirmHint() and by activateCell(), which may not learn
// different answers -- the label is what the hint slot quotes, so the bar and
// the binding cannot disagree about what the focused cell does.
int functionAt(int cell) {
  const int at = cell - kCharCells;
  return (at >= 0 && at < kFnCount) ? at : -1;
}

std::string upperOf(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

}  // namespace

TextEntryScreen::TextEntryScreen(TextEntryConfig config)
    : GridFocusScreen(GridFocus({10, 10, 10, 10, kFnCount})), config_(std::move(config)) {
  // The side buttons move along a row and the front buttons change row --
  // the board's own note, and a mechanism ReaderScreen and PeekScreen already
  // use rather than one this screen invents.
  declareSplitMovers();
  rebuildCells();
  syncVm();
}

void TextEntryScreen::rebuildCells() {
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
  vm_.cells.reserve(static_cast<size_t>(kCharCells + kFnCount));
  for (const char c : chars) vm_.cells.push_back(std::string(1, c));
  // THE LAYER KEY IS THE ONE CELL OF THE ROW THAT IS NOT A CONSTANT AT ALL --
  // and the confirm cell is the one the CALLER names. Both are substituted
  // here rather than held as a second row literal: a second array would be a
  // second spelling of the cells that do not change, which is what the dead
  // `kLower` at the top of this file already cost once.
  vm_.cells.push_back(kCaretLeft);
  vm_.cells.push_back(kCaretRight);
  vm_.cells.push_back(kShiftKey);
  vm_.cells.push_back(layer_ == Layer::Symbols ? kBaseKey : kSymbolKey);
  vm_.cells.push_back(kSpaceKey);
  vm_.cells.push_back(config_.confirmLabel);
  vm_.rowWidths = {10, 10, 10, 10, kFnCount};
}

void TextEntryScreen::setLayer(Layer l) {
  if (layer_ == l) return;
  layer_ = l;
  rebuildCells();
  syncVm();
}

void TextEntryScreen::setEntered(std::string text) {
  if (text.size() > config_.maxLength) text.resize(config_.maxLength);
  entered_ = std::move(text);
  // AT THE END, which is where somebody arriving from Wi-Fi's EDIT PASSWORD
  // wants it: the usual reason to come back is that the last characters were
  // wrong.
  caret_ = entered_.size();
  syncVm();
}

bool TextEntryScreen::moveCaret(int delta) {
  const int at = static_cast<int>(caret_) + delta;
  const int clamped = at < 0 ? 0 : (at > static_cast<int>(entered_.size())
                                        ? static_cast<int>(entered_.size())
                                        : at);
  // CLAMPS RATHER THAN WRAPPING, which is Focus::set's rule and for its
  // reason: a caret that jumped from the start of a string to its end would
  // be indistinguishable from a misread press.
  if (static_cast<size_t>(clamped) == caret_) return false;
  caret_ = static_cast<size_t>(clamped);
  syncVm();
  return true;
}

void TextEntryScreen::syncVm() {
  vm_.title = config_.title;
  vm_.fieldName = config_.fieldName;
  vm_.entered = entered_;
  vm_.caret = caret_;
  vm_.counter = std::to_string(entered_.size()) + " CHARS";
  vm_.visibility = config_.visibility;
  vm_.note = "UP AND DOWN MOVE BETWEEN ROWS; THE SIDE PAGE BUTTONS MOVE ALONG A ROW.";
  vm_.focusedCell = focus();
  // THE NOTE IS THE MECHANISM'S AND NOT THE CALLER'S, unlike every string
  // above it: it describes the input model this base class owns, so it is the
  // same sentence for every caller by construction and a parameter would only
  // be a chance for one of them to describe a keyboard it does not have.
  //
  // THE CONFIRM LABEL NAMES WHAT THE FOCUSED CELL DOES, and says nothing when
  // the cell does nothing. It was a constant `TYPE`, which is false on three
  // of the function keys and outright misleading on the confirm cell -- a
  // Confirm labelled TYPE that leaves the screen and starts a join. Settings
  // is the precedent for a label that varies within a screen and WifiSettings
  // is the second; the board carries the rule.
  //
  // The EMPTY slot on an unusable confirm is the existing vocabulary rather
  // than a new one: a button with no action gets an empty slot, drawn at
  // kHintEmptySlotW rather than as nothing. activateCell asks `confirmable()`
  // too, so the bar and the behaviour cannot disagree.
  //
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
  vm_.hints = {deletes ? "DELETE" : "BACK", confirmHint(), "UP", "DOWN"};
  vm_.holds = {deletes, false, false, false};
  declareHints(vm_.holds);
}

std::string TextEntryScreen::confirmHint() const {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(vm_.cells.size())) return "TYPE";
  switch (functionAt(f)) {
    // SPACE is not in this list, and that is the distinction: it types a
    // character like any other cell and is the only function key that does.
    case kFnCaretLeft:
    case kFnCaretRight:
      return "MOVE";
    // The key's own label, whichever layer it is currently offering.
    case kFnShift:
    case kFnLayer:
      return vm_.cells[static_cast<size_t>(f)];
    case kFnConfirm:
      return confirmable() ? config_.confirmLabel : "";
    default:
      return "TYPE";
  }
}

Action TextEntryScreen::activateCell() {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(vm_.cells.size())) return Action::none();

  switch (functionAt(f)) {
    case kFnShift:
      // One-shot: armed here, spent by the next character.
      shiftArmed_ = !shiftArmed_;
      setLayer(shiftArmed_ ? Layer::Upper : Layer::Lower);
      return Action::redraw();
    case kFnLayer:
      // EITHER SPELLING, because the label is the state: `abc` is this same
      // key showing the layer it goes back to, so the branch is the same one
      // and `setLayer` reads the state rather than the word on the cell.
      //
      // Latching, and it clears a pending shift: the two modifiers are
      // exclusive, so leaving shift armed under the symbol page would make the
      // next symbol turn it off for no visible reason.
      shiftArmed_ = false;
      setLayer(layer_ == Layer::Symbols ? Layer::Lower : Layer::Symbols);
      return Action::redraw();
    case kFnCaretLeft:
      return moveCaret(-1) ? Action::redraw() : Action::none();
    case kFnCaretRight:
      return moveCaret(+1) ? Action::redraw() : Action::none();
    case kFnConfirm:
      // TOO SHORT, so the press does nothing -- and the bar has already said
      // so with an empty Confirm slot, which is what keeps this from being the
      // focuses-then-ignores defect. One spelling (`confirmable()`) answers
      // both. With the default floor of 0 this branch is unreachable and the
      // cell is live on an empty field, which is the point of that default.
      if (!confirmable()) return Action::none();
      confirmed_ = true;
      // LATCHED, NOT POPPED. The shell reads confirmed() and entered() off
      // this screen while it is still standing and then decides where the flow
      // goes -- a pop would destroy the object holding the text.
      return config_.confirmAction;
    default:
      break;
  }

  const std::string text = (functionAt(f) == kFnSpace) ? " " : vm_.cells[static_cast<size_t>(f)];
  // TYPING STOPS AT THE BOUND rather than silently dropping the keypress: a
  // keyboard that swallows a press is indistinguishable from one that missed
  // it.
  if (entered_.size() + text.size() > config_.maxLength) return Action::none();
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

Action TextEntryScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      // DELETE while there is something to delete, and LEAVE when there is
      // not -- which is what the bar says in each state. This returned
      // none() on an empty field, so the press was a dead button and the
      // hold was the only way off the screen.
      //
      // IT RETURNS THE CALLER'S ACTION rather than popping, exactly as the
      // hold does: for Wi-Fi the radio was brought up for this join and the
      // shell has to be told. Leaving by either route is the same event, so
      // it sets the same flag.
      if (entered_.empty()) {
        cancelled_ = true;
        return config_.leaveAction;
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
      // The held Back, which is the way off this screen with something typed.
      // It returns the caller's Action rather than popping because the caller
      // has work to do, and because cancelled() is unreadable from a destroyed
      // screen.
      cancelled_ = true;
      return config_.leaveAction;
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

void TextEntryScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderTextEntry(fb, fonts, vm_, plane);
}

}  // namespace reader
