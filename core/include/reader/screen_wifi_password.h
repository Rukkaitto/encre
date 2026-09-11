#pragma once
#include <string>
#include <vector>

#include "reader/grid_focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// design/WifiPassword.dc.html -- THE FIRST TEXT ENTRY IN THIS FIRMWARE. There
// is no caret, no editable string, no character set and no keyboard anywhere
// in core/, shell/ or sim/ before this; it brings all four.
//
// THREE LAYERS, ALL EXACTLY 10x4, over a function row of four. A WPA2
// passphrase is any printable ASCII, 8 to 63 characters, so all 95 have to be
// reachable or some passwords are untypeable on this device -- which is
// asserted rather than assumed, in test_screen_wifi_password.cpp.
//
// EVERY LAYER IS THE SAME 40 CELLS, so the panel geometry never moves and
// GridFocus never has to cope with a changing shape. That constraint is what
// decides the layout: the symbol layer repeats the digits, because 28 of the
// 32 punctuation characters are not on the base layer and 28 does not fill 40
// -- and repeating digits is what a symbol page is for anyway, so nobody has
// to switch back to type one.
//
// THE INPUT MODEL ALREADY EXISTS. declareSplitMovers is what ReaderScreen and
// PeekScreen use: with it, Up/Down arrive as AltPrev/AltNext and the SIDE
// buttons as Prev/Next -- exactly the board's "up and down move between rows;
// the side page buttons move along a row".
//
// BACK DELETES AND A HELD BACK LEAVES, which is spec 4.1b's own wording and
// the hold the board has drawn a ring for since it was authored. It is the
// only way off this screen, so the ring is not decoration.
class WifiPasswordScreen : public GridFocusScreen {
 public:
  // 802.11's own bound on a WPA2 passphrase. Typing stops here rather than
  // silently dropping characters, because a keyboard that swallows a keypress
  // is indistinguishable from one that missed it.
  static constexpr size_t kMaxPassphrase = 63;
  // AND ITS FLOOR, from the same clause of 802.11 and spec 4.1b's own "8 to
  // 63". This keyboard is only ever reached for a LOCKED network -- an open
  // one joins directly -- so JOIN under eight characters cannot succeed: it
  // would spend a radio round trip and a failure dialog to report a length
  // the counter is already showing. The cell goes inert and the Confirm slot
  // goes EMPTY, which is this firmware's existing vocabulary for a button
  // with no action rather than a new one.
  static constexpr size_t kMinPassphrase = 8;
  // Whether JOIN would do anything right now. ONE SPELLING, asked by the hint
  // bar and by the press -- two would be free to drift, and the drift is a
  // cell that promises JOIN and ignores Confirm.
  bool joinable() const { return entered_.size() >= kMinPassphrase; }

  explicit WifiPasswordScreen(std::string ssid);

  ScreenId id() const override { return ScreenId::WifiPassword; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const WifiPasswordViewModel& vm() const { return vm_; }
  const std::string& entered() const { return entered_; }
  // WHERE THE NEXT CHARACTER GOES, as a byte offset into entered(). Exposed
  // for the tests; the theme reads vm().caret.
  size_t caret() const { return caret_; }

  // EDIT PASSWORD comes back here with what was already typed, which is the
  // whole reason that slab exists -- see WifiErrorScreen. Clamped to the
  // maximum, because a record from anywhere else is not this screen's to
  // trust.
  void setEntered(std::string text);

  // Whether JOIN was pressed. The shell starts the join; this screen owns no
  // radio -- and it reads entered() off this same object, which is the
  // sharpest reason the pop had to go: the pop destroyed the passphrase.
  // READ IT WHILE THIS SCREEN IS STILL ON TOP. It latches and returns
  // Action::wifi(), which pops NOTHING, so the shell reads the outcome on the
  // dispatch's own pass and pops afterwards. This said "the shell reads it
  // after the pop", and after a pop there is no screen left to ask:
  // App::dispatch's Pop is `stack_.pop_back()`, which destroys the object. See
  // Action::wifi(), and App::wifiRequested() for the order.
  bool joinChosen() const { return join_; }
  // Whether a held Back asked to leave without joining. Same rule, and the
  // shell has work to do on it: the radio was brought up for this join.
  bool cancelled() const { return cancelled_; }

  // Which layer is showing. Exposed for the simulator and the goldens, which
  // render all three: they are the same 44 cells with different glyphs, so
  // they are goldens rather than boards.
  enum class Layer { Lower, Upper, Symbols };
  Layer layer() const { return layer_; }
  void setLayer(Layer l);

 protected:
  void syncVm() override;

 private:
  // The 40 character cells of the showing layer, plus the four function keys.
  void rebuildCells();
  // What the Confirm slot says, which is what pressing the focused cell
  // does -- or nothing, when it does nothing.
  std::string confirmLabel() const;
  // What pressing the focused cell does.
  Action activateCell();

  // MOVES THE CARET, clamped. Returns whether it went anywhere, so a press at
  // either end costs no ~520 ms repaint.
  bool moveCaret(int delta);

  std::string ssid_;
  std::string entered_;
  // A BYTE OFFSET, not a character index, and the distinction is safe here
  // for a reason worth stating: a WPA2 passphrase is printable ASCII, which
  // this keyboard enforces by construction -- every cell emits exactly one
  // byte. So one byte is one character and the caret cannot land inside a
  // multi-byte sequence. setEntered is the only door a non-ASCII string could
  // come through and it is the shell's own stored secret, which this keyboard
  // wrote.
  size_t caret_ = 0;
  WifiPasswordViewModel vm_;
  Layer layer_ = Layer::Lower;
  // SHIFT IS ONE-SHOT and the LAYER KEY LATCHES. A passphrase usually needs one
  // capital, so a shift that stayed on would cost a second press to turn off far
  // more often than it saved one; a symbol page is the opposite, because
  // somebody typing punctuation usually types several.
  //
  // WHICH IS ALSO WHY ONLY THE LAYER KEY CHANGES ITS LABEL -- `#+=` on the two
  // letter layers, `abc` while the symbol layer is showing. A latched mode needs
  // a way off it that says so; a one-shot has no state for a label to name. See
  // kSymbolKey in the .cpp, and design/WifiPassword.dc.html.
  bool shiftArmed_ = false;
  bool join_ = false;
  bool cancelled_ = false;
};

}  // namespace reader
