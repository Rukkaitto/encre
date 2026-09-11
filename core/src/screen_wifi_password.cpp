#include "reader/screen_wifi_password.h"

#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

// THE BOARD'S OWN BASE LAYER: a-j / k-t / u-z 0 1 2 3 / 4-9 . - _ !
constexpr const char* kLower = "abcdefghijklmnopqrstuvwxyz0123.-_!";
// 26 letters + 0-3 on row three, then 4-9 and four punctuation on row four.
// Written out rather than generated so the board and the code can be read
// side by side.
constexpr const char* kBaseTail = "456789.-_!";

// The 28 punctuation characters the base layer does NOT carry, then the two
// most useful repeats, over a row of digits. 10 + 30 = 40.
//
// THE UNION OF THE THREE LAYERS IS ALL 95 PRINTABLE ASCII, which is what a
// WPA2 passphrase may contain -- asserted in the test rather than counted by
// eye here.
constexpr const char* kSymbolTail = "\"#$%&'()*+,/:;<=>?@[\\]^`{|}~.-";

const char* kFunctionRow[4] = {"SHIFT", "#+=", "SPACE", "JOIN"};

std::string upperOf(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

}  // namespace

WifiPasswordScreen::WifiPasswordScreen(std::string ssid)
    : GridFocusScreen(GridFocus({10, 10, 10, 10, 4})), ssid_(std::move(ssid)) {
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
  vm_.cells.reserve(44);
  for (const char c : chars) vm_.cells.push_back(std::string(1, c));
  for (const char* f : kFunctionRow) vm_.cells.push_back(f);
  vm_.rowWidths = {10, 10, 10, 10, 4};
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
  syncVm();
}

void WifiPasswordScreen::syncVm() {
  vm_.title = "PASSWORD";
  vm_.ssid = ssid_;
  // SHOWN IN CLEAR, which is the board's `SHOWN WHILE TYPING`: at one
  // character per ~520 ms repaint on a 44-cell grid, a typo you cannot see is
  // punishing, and this is a device you hold.
  vm_.entered = entered_;
  vm_.counter = std::to_string(entered_.size()) + " CHARS";
  vm_.visibility = "SHOWN WHILE TYPING";
  vm_.note = "UP AND DOWN MOVE BETWEEN ROWS; THE SIDE PAGE BUTTONS MOVE ALONG A ROW.";
  vm_.focusedCell = focus();
  // BACK DELETES AND A HELD BACK LEAVES -- spec 4.1b, and the ring the board
  // draws. It is the only way off this screen, which is why the hold is bound
  // rather than merely drawn.
  vm_.hints = {"DELETE", "TYPE", "UP", "DOWN"};
  vm_.holds = {true, false, false, false};
  declareHints(vm_.holds);
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
  if (cell == "JOIN") {
    join_ = true;
    return Action::pop();
  }

  const std::string text = (cell == "SPACE") ? " " : cell;
  // TYPING STOPS AT THE BOUND rather than silently dropping the keypress: a
  // keyboard that swallows a press is indistinguishable from one that missed
  // it.
  if (entered_.size() + text.size() > kMaxPassphrase) return Action::none();
  entered_ += text;
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
      // DELETE, not leave. The bar says so, and the hold is the way out.
      if (entered_.empty()) return Action::none();
      entered_.pop_back();
      syncVm();
      return Action::redraw();
    case Gesture::Secondary:
      cancelled_ = true;
      return Action::pop();
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
