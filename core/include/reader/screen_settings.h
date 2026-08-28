#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/settings.h"
#include "reader/viewmodel.h"

namespace reader {

// Where a changed setting GOES. An interface rather than a std::function for the
// reason app.h's ScreenFactory gives: `-fno-exceptions` makes a std::function's
// allocation an abort() with no diagnostic, and the callers here are all long-lived
// objects that can simply be pointed at.
//
// It does TWO jobs and the name is `commit` because of it: the shell both writes
// the file and applies the change to whatever is already running (the idle timer
// for `sleepAfterMs`, the refresh policy for the other two). Splitting them would
// invite a screen that persists a value nothing acts on until the next boot, which
// on a setting like "refresh on screen change" is indistinguishable from the
// setting not working.
//
// False means the write failed. The screen still shows the new value, because the
// change HAS taken effect in RAM -- refusing to show it would make a card that has
// gone read-only look like a screen that ignores its buttons. The shell logs the
// failure; `core/` never learns why, which is what keeps filesystems out of here.
class SettingsSink {
 public:
  virtual ~SettingsSink() = default;
  virtual bool commit(const Settings& s) = 0;
};

// design/Settings.dc.html.
//
// THE SCREEN DRAWS EVERY BOARD ROW AND ONLY SOME RESPOND. `Sleep screen` is the
// last one with nothing behind it -- covers are issue #11 -- and rather than let it
// be selected and do nothing when pressed, FOCUS SKIPS IT. A row that cannot be
// reached cannot mislead; a row that focuses and then ignores CHANGE is the silent
// no-op this project has been bitten by twice. It is drawn identically to an
// unfocused focusable row: no dimming, because a visual difference nobody designed
// is worse than none.
//
// IT USED TO SKIP FIVE MORE. A TYPOGRAPHY section carried Font, Size, Margins, Line
// spacing and Alignment, drawn and unreachable because the settings behind them did
// not exist. They do now, and they are edited on their own screen -- so those five
// readout rows became one `Typography` row that opens it. That is what makes this
// screen the FIRST here whose Confirm hint varies within itself: OPEN on that row,
// CHANGE on the four DEVICE rows. See syncVm.
//
// The list FITS the panel today -- seven items where eleven fit -- but it will grow
// again, so it SCROLLS, with the same rail Library uses, taken off `totalRows >
// rows` rather than assumed. Section headers are items in that list: they scroll
// with the rows, they are never focusable, and they count toward the rail's
// proportion. Treating them as anything else would make the rail lie about how much
// list there is.
class SettingsScreen : public FocusScreen {
 public:
  // `sink` may be null -- the simulator and the golden tests have nowhere to
  // persist to, and a screen that could not be rendered without a filesystem
  // would not be renderable on the desktop at all.
  SettingsScreen(const Settings& initial, SettingsSink* sink);

  ScreenId id() const override { return ScreenId::Settings; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // focus()/setFocus() are FocusScreen's -- final, one mechanism. setFocus still
  // refuses an index that is not focusable rather than silently landing on a
  // section header (a restored focus that cannot be moved off would be worse
  // than no restore) -- that rule now comes from focusable() below, through
  // Focus::Gate, along with the skip-past-headers stepping this screen used to
  // hand-roll. The hand-rolled walk silently stopped wrapping once; the gate
  // cannot, because it is the same code every other list exercises.

  // How much room the list has, and what each kind of item costs -- from
  // Theme::settingsMetrics. The COUNTING happens here because the item table is
  // here: how many fit depends on which items are headers, and the theme does not
  // know that. Call it before the first paint, as Library's setVisibleRows must
  // be called, or the list correctly renders empty.
  void setMetrics(int listH, int rowH, int headerH);

  const SettingsViewModel& vm() const { return vm_; }
  const Settings& settings() const { return settings_; }

  // Which setting a row edits, or `Typography`, which edits none and opens the
  // screen that does.
  //
  // `Typography` IS NOT A SETTING AND IS STILL FOCUSABLE, which is why
  // `field != None` can no longer serve as the focusability test -- it used to mean
  // both "has a setting" and "can be focused", and those are two facts now. See
  // `reachable` below.
  //
  // Public only so the row TABLE can live in the .cpp beside the code that reads
  // it -- keeping the table next to the board's order is what makes it checkable
  // by eye against design/Settings.dc.html, which is worth more than the
  // encapsulation of two descriptive types.
  enum class Field { None, Typography, SleepAfter, FullRefresh, OnTransition };

  struct Item {
    const char* label;
    Field field;
    bool isHeader;
    // Whether a focus may land here. NOT derivable from `field`: a header has no
    // field and cannot be focused, `Sleep screen` has no field and cannot be
    // focused, and `Typography` has no field and MUST be -- it discloses a screen
    // rather than editing a value.
    bool reachable;
    // What a row with no setting behind it shows. A board placeholder, NOT a
    // setting: the value design/Settings.dc.html states, kept so the screen matches
    // the board before the setting behind it exists. `Sleep screen` is the only one
    // left -- covers are issue #11. Empty for a row whose value comes from
    // `settings_`, and empty for a row that discloses instead of stating one.
    const char* placeholder;
  };

 private:
  Action cycleFocused();
  // Which rows a focus may land on: not a header, and marked `reachable`.
  // Consumed by FocusScreen through Focus::Gate.
  bool focusable(int index) const override;
  void syncVm() override;
  int firstFocusable() const;

  Settings settings_;
  SettingsSink* sink_;
  SettingsViewModel vm_;
};

}  // namespace reader
