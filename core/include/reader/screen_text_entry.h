#pragma once
#include <cstddef>
#include <string>

#include "reader/grid_focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// TYPING A STRING ON A DEVICE WITH SIX BUTTONS. The keyboard that shipped as
// WifiPasswordScreen, with everything Wi-Fi-shaped lifted into TextEntryConfig
// -- which is THE SECOND COPY IS THE EXTRACTION POINT applied before the second
// copy exists rather than five copies late. That rule was retrofitted across two
// whole passes here (`FocusScreen` and the shared-primitives sweep) and the cost
// of retrofitting is why it is a rule; #126 is it being followed on time.
//
// WHAT IS MECHANISM AND STAYS HERE -- none of it is Wi-Fi-shaped:
//   - 46 cells: three layers of 10x4 characters over a function row of six,
//     whose union is all 95 printable ASCII.
//   - a caret that is a POSITION rather than a terminator, so a typo three
//     characters back can be fixed without unwinding to it.
//   - a one-shot SHIFT and a LATCHING layer key, and the label rule that falls
//     out of the difference.
//   - declareSplitMovers, so Up/Down change row and the SIDE buttons move along
//     one -- ReaderScreen and PeekScreen's mechanism, not a mapping invented here.
//   - Back deletes before the caret and LEAVES when the field is empty.
//
// THREE LAYERS, ALL EXACTLY 10x4, over a function row of six. EVERY LAYER IS THE
// SAME 46 CELLS, so the panel geometry never moves and GridFocus never has to
// cope with a changing shape. That constraint is what decides the layout: the
// symbol layer repeats the digits, because 28 of the 32 punctuation characters
// are not on the base layer and 28 does not fill 40 -- and repeating digits is
// what a symbol page is for anyway, so nobody has to switch back to type one.
//
// THE UNION BEING ALL 95 IS ASSERTED RATHER THAN ASSUMED, in
// test_screen_text_entry.cpp: a character on no layer is a string that cannot be
// typed on this device at all, with nothing on the glass to say so.
//
// A BASE CLASS AND NOT A SCREEN. It has no `id()` -- ScreenId gains no member
// for this -- so a caller derives, names its own id and hands over a config.
// That is also what keeps the factory, the session record and the restore
// catalogue untouched by the extraction.
struct TextEntryConfig {
  // The header band's two slots. The left says what is being typed
  // (`PASSWORD`); the right says what it is FOR -- a network's name for Wi-Fi,
  // a field's name for anything with more than one field. The band is the only
  // place on this screen that can say either, because the field itself is full
  // of what the reader is typing.
  std::string title;
  std::string fieldName;

  // The last cell of the function row, and the Confirm hint while that cell is
  // focused. `JOIN` for Wi-Fi. It is the ACTION's label and so the caller's:
  // a keyboard has no opinion about what finishing means.
  std::string confirmLabel;

  // The counter row's right-hand claim -- `SHOWN WHILE TYPING` for Wi-Fi.
  //
  // IT IS THE CALLER'S BECAUSE THE DECISION IS. The Wi-Fi board's argument for
  // showing a passphrase in clear is that "at one character per ~520 ms repaint
  // on a 46-cell grid, a typo you cannot see is punishing" -- which is about a
  // passphrase you own, not an account password typed on a train. So the string
  // is a parameter and the mechanism has no opinion.
  //
  // WHAT IS DELIBERATELY NOT HERE IS A MASK. The text goes into the view-model
  // in clear on every path today, and whether to offer a masked mode is a
  // DESIGN call that wants a board rather than a derivation (#126, trap 3). It
  // is not foreclosed: masking is one more config field and one branch where
  // syncVm() mirrors `entered_`, and this label is already the thing that would
  // have to change beside it.
  std::string visibility;

  // WHERE TYPING STOPS. A press past it does NOTHING rather than being accepted
  // and dropped, because a keyboard that swallows a keypress is
  // indistinguishable from one that missed it. Required: a config that leaves
  // it at 0 is a keyboard that cannot type, which its own caller's first test
  // will say.
  size_t maxLength = 0;

  // AND ITS FLOOR, WHICH DEFAULTS TO NONE -- read the zero before copying a
  // number into it.
  //
  // A MINIMUM MAKES THE CONFIRM CELL INERT AND BLANKS THE CONFIRM SLOT, which
  // is this firmware's vocabulary for a button with no action. That is correct
  // for Wi-Fi, where 802.11 says a WPA2 passphrase is 8 to 63 characters and a
  // shorter one cannot join: the press would spend a radio round trip and a
  // failure dialog to report a length the counter is already showing.
  //
  // IT IS WRONG FOR ALMOST ANYTHING ELSE, AND CARRYING IT ALONG WOULD HAVE BEEN
  // A DEAD BUTTON ON A LEGAL STATE -- a defect class this project has shipped
  // twice and refuses. Instapaper's own documentation is the case that named
  // it: "Passwords are not required, and many users do not have one. Any
  // interface that prompts for credentials must accommodate this. For example,
  // you cannot treat an empty password as a user error." An empty field is a
  // legal value there, so the confirm cell must be live on it.
  //
  // So the floor is 802.11's and belongs to the Wi-Fi call site alone. The
  // default here is 0 and a test says so, because a default nobody checks is
  // how a number like this travels.
  size_t minLength = 0;

  // WHAT THE TWO OUTCOMES RETURN. Both are LATCHES today -- the screen records
  // that it happened and the shell reads it off the screen that is still
  // standing -- because both Wi-Fi outcomes need the passphrase, and a pop
  // destroys the object holding it. See Action::wifi() and App::wifiRequested().
  //
  // TWO FIELDS AND NOT ONE, although Wi-Fi passes the same Action for both: a
  // confirm and a leave are two events, the shell already tells them apart by
  // confirmed() and cancelled(), and a caller whose leave is an ordinary
  // Action::pop() should not have to take a latch with it.
  Action confirmAction;
  Action leaveAction;
};

class TextEntryScreen : public GridFocusScreen {
 public:
  explicit TextEntryScreen(TextEntryConfig config);

  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const TextEntryViewModel& vm() const { return vm_; }
  const std::string& entered() const { return entered_; }
  // WHERE THE NEXT CHARACTER GOES, as a byte offset into entered(). Exposed for
  // the tests; the theme reads vm().caret.
  size_t caret() const { return caret_; }

  // Whether the confirm cell would do anything right now. ONE SPELLING, asked
  // by the hint bar and by the press -- two would be free to drift, and the
  // drift is a cell that promises an action and ignores Confirm.
  bool confirmable() const { return entered_.size() >= config_.minLength; }

  // Puts a string in the field, clamped to the maximum -- a string from
  // anywhere else is not this screen's to trust. Wi-Fi's EDIT PASSWORD comes
  // back through here with what was already typed, which is the whole reason
  // that slab exists.
  void setEntered(std::string text);

  // Whether the confirm cell was pressed, and whether a Back asked to leave
  // without confirming. READ THEM WHILE THIS SCREEN IS STILL ON TOP: both
  // latch and return the caller's Action, which pops nothing, so the shell
  // reads the outcome on the dispatch's own pass and pops afterwards. After a
  // pop there is no screen left to ask -- App::dispatch's Pop is
  // `stack_.pop_back()`, which destroys the object.
  bool confirmed() const { return confirmed_; }
  bool cancelled() const { return cancelled_; }

  // Which layer is showing. Exposed for the simulator and the goldens, which
  // render all three: they are the same 46 cells with different glyphs, so they
  // are goldens rather than boards.
  enum class Layer { Lower, Upper, Symbols };
  Layer layer() const { return layer_; }
  void setLayer(Layer l);

 protected:
  void syncVm() override;
  const TextEntryConfig& config() const { return config_; }

 private:
  // The 40 character cells of the showing layer, plus the six function keys.
  void rebuildCells();
  // What the Confirm slot says, which is what pressing the focused cell does --
  // or nothing, when it does nothing.
  std::string confirmHint() const;
  // What pressing the focused cell does.
  Action activateCell();
  // MOVES THE CARET, clamped. Returns whether it went anywhere, so a press at
  // either end costs no ~520 ms repaint.
  bool moveCaret(int delta);

  TextEntryConfig config_;
  std::string entered_;
  // A BYTE OFFSET, not a character index, and the distinction is safe for a
  // reason worth stating: every cell emits exactly one byte of printable ASCII,
  // which this keyboard enforces by construction, so one byte is one character
  // and the caret cannot land inside a multi-byte sequence. setEntered is the
  // only door a non-ASCII string could come through and it is the caller's own
  // stored string, which this keyboard wrote.
  size_t caret_ = 0;
  TextEntryViewModel vm_;
  Layer layer_ = Layer::Lower;
  // SHIFT IS ONE-SHOT and the LAYER KEY LATCHES. A password usually needs one
  // capital, so a shift that stayed on would cost a second press to turn off far
  // more often than it saved one; a symbol page is the opposite, because
  // somebody typing punctuation usually types several.
  //
  // WHICH IS ALSO WHY ONLY THE LAYER KEY CHANGES ITS LABEL -- `#+=` on the two
  // letter layers, `abc` while the symbol layer is showing. A latched mode needs
  // a way off it that says so; a one-shot has no state for a label to name. See
  // kSymbolKey in the .cpp, and design/WifiPassword.dc.html.
  bool shiftArmed_ = false;
  bool confirmed_ = false;
  bool cancelled_ = false;
};

}  // namespace reader
