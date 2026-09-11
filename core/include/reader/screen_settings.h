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
// EVERY ROW HERE RESPONDS NOW. `Sleep screen` / `BOOK COVER` was the last one
// drawn with nothing behind it -- issue #11 -- and it is gone: the SLEEP SCREEN
// section replaces it with `Shows` and `Cover fit`, two rows that act.
//
// FOCUS STILL SKIPS WHAT CANNOT ACT, and there is still one case: `Cover fit` is
// unreachable while `Shows` shows no cover, DERIVED from settings_ rather than
// tabulated -- Typography's own precedent, where `Font` is unreachable while one
// body face is vendored and becomes reachable the moment a second lands, with no
// line to remember. A row that cannot be reached cannot mislead; a row that
// focuses and then ignores CHANGE is the silent no-op this project has been
// bitten by twice. It is drawn identically to an unfocused focusable row: no
// dimming, because a visual difference nobody designed is worse than none.
//
// IT USED TO SKIP FIVE MORE. A TYPOGRAPHY section carried Font, Size, Margins, Line
// spacing and Alignment, drawn and unreachable because the settings behind them did
// not exist. They do now, and they are edited on their own screen -- so those five
// readout rows became one `Typography` row that opens it. That is what makes this
// screen the FIRST here whose Confirm hint varies within itself: OPEN on that row,
// CHANGE on the five that edit a value in place. See syncVm.
//
// The list FITS the panel today -- nine items where eleven fit -- but it will grow
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
  enum class Field {
    None,
    Typography,
    SleepShows,
    CoverFit,
    SleepAfter,
    FullRefresh,
    OnTransition,
    Wifi
  };

  // WHICH SCREEN A ROW DISCLOSES, IF ANY -- the one spelling of that question,
  // and it has to be one: three separate places ask it (the chevron in syncVm,
  // the Confirm hint beside it, and the push in onGesture), and three spellings
  // of "this row opens something" is how a row draws a chevron, promises OPEN,
  // and then cycles a value it does not have. That is the drifting-condition
  // defect this project has shipped twice, both times as a dead button.
  //
  // It was `field == Field::Typography` written out three times, which was
  // correct while one row disclosed and became a maintenance trap the moment a
  // second did.
  static bool disclosedScreen(Field f, ScreenId& out);

  struct Item {
    const char* label;
    Field field;
    bool isHeader;
    // Whether a focus may land here AT ALL. NOT derivable from `field`: a header
    // has no field and cannot be focused, and `Typography` has no field and MUST
    // be -- it discloses a screen rather than editing a value.
    //
    // It is a CEILING, not the answer: focusable() reads this AND asks the
    // settings, because `Cover fit` is reachable only while `Shows` shows a cover.
    // A row that is `false` here can never be focused; a row that is `true` may
    // still be refused by a condition no table can hold.
    bool reachable;
    // `Item::placeholder` IS GONE. It carried the BOARD's value for a row whose
    // setting did not exist yet, and `Sleep screen` / `BOOK COVER` was its last
    // producer -- that row is now two rows that read `settings_`. The field is
    // removed rather than left with no writer, which is how this project handles
    // an outlived member (`ListRow::trackingEm1000` is the counter-example that
    // stayed and had to be pinned by a test to keep it honest). A test asserts
    // that every drawn row either discloses a screen or states a value, so the
    // removal is a stated fact rather than an assumption.
  };

 private:
  Action cycleFocused();
  // Which rows a focus may land on: not a header, marked `reachable`, and -- for
  // `Cover fit` -- only while `Shows` shows a cover. Consumed by FocusScreen
  // through Focus::Gate, so it is re-asked on every step rather than cached: the
  // answer changes when the row above it is cycled.
  bool focusable(int index) const override;
  void syncVm() override;
  int firstFocusable() const;

  Settings settings_;
  SettingsSink* sink_;
  SettingsViewModel vm_;
};

}  // namespace reader
