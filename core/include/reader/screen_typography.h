#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/screen_settings.h"  // SettingsSink -- the same sink, see below
#include "reader/settings.h"
#include "reader/viewmodel.h"

namespace reader {

class GlyphSource;

// design/Typography.dc.html.
//
// A FULL SCREEN, NOT AN OVERLAY, despite being reached from one: the board has its
// own header band, its own hint bar and an opaque background, so it clears the
// framebuffer and nothing of the page under it is visible. That is also what makes
// the live preview affordable -- the reader's page and metrics are stale for as
// long as this screen stands, and nothing draws them.
//
// --- IT SHARES SettingsSink, AND THAT IS DELIBERATE ---------------------------
//
// The typography fields live in `Settings` with everything else, so the sink that
// applies and persists a Settings change is already the right shape. A second
// interface would be a second thing to wire, a second null case, and a second
// place for "applied but not saved" to be got wrong.
//
// Its contract is unchanged and it is the contract that matters: it APPLIES and
// PERSISTS, in that order, and a refused write still leaves the new value on
// screen -- because the change HAS taken effect, and reverting the display would
// make a read-only card look like a screen that ignores its buttons.
//
// --- ONE MODE, AND CHANGE CYCLES IN PLACE -------------------------------------
//
// Up/Down move the focus, wrapping. Confirm (`CHANGE`) cycles the focused row's
// value forward, wrapping. Back leaves.
//
// AN EARLIER DESIGN HAD TWO MODES and it was rejected on the rendered board: it
// read `DONE / EDIT / UP / DOWN` browsing and `DONE / OK / UP / DOWN` editing, and
// `DONE` and `OK` are synonyms -- two words for "finished" and nothing to say that
// one finished the ROW and the other left the SCREEN. Cycling in place is what
// Settings already does, and its argument transfers unchanged: "this is one
// button, so there is no way back except round. Five sleep steps and three
// cadences keeps a full cycle short enough to be usable on a panel that costs
// ~520 ms a repaint." Two screens editing a value list two different ways would
// have been two mechanisms for one job.
//
// WHAT IT COSTS is one direction: five values is at most four presses from any
// value to any other, the same worst case Settings accepted.
//
// --- THE VALUES AND THE FOCUS BOTH WRAP ---------------------------------------
//
// Every list in this firmware wraps, and the recorded hazard is not the wrap: it
// is AUTO-REPEAT, where "a wrap belongs to a press and a hold rests at the end",
// which is why Focus::move(delta, held) clamps for a held button.
//
// THIS SCREEN DECLARES NO REPEAT, as Settings does not, and it must not -- every
// size step re-inits the body face, so a held button would race through the sizes
// re-rasterising the alphabet on each one. One step per press, and the sharp edge
// on wrapping never arises.
class TypographyScreen : public FocusScreen {
 public:
  // WHAT THE PREVIEW SAYS. A FIXED specimen, not the book's own text.
  //
  // The book's text would mean reaching down the stack for the Reader's laid page
  // on a screen that is otherwise independent of it, and it would make the box's
  // height depend on content that changes. A fixed string is predictable at every
  // size, which is what lets the box be sized by the panel instead.
  //
  // Middlemarch's opening sentence, which is the board's own copy: it has to fill
  // a box the panel sizes, and the truncated form left a quarter of it empty at
  // the default size. It wraps to four lines in Chrome at both geometries, which
  // is the count the preview box is sized around.
  //
  // AND IT PREVIEWS THE MARGINS TOO, WHICH THIS COMMENT ONCE DENIED. It said the
  // box "cannot preview the margins" because the box is chrome geometry -- the
  // board's 24px page margins less its own border and padding -- where the reading
  // column is `panelW - 2 * margins`, so the two measures never agree. Both facts
  // are still true and the conclusion drawn from them was wrong: THE BOX IS THE
  // PAGE AND ITS SIDE PADDING IS THE MARGIN, so the padding tracks the setting and
  // the base measure being narrower than the column is beside the point. The
  // BORDER does not move, so nothing about the fixed box height changes.
  //
  // Reported off the device as "changing the margins doesn't update the live
  // preview". All four editable rows show in the box now.
  static constexpr const char* kSpecimen =
      "Miss Brooke had that kind of beauty which seems to be thrown into relief "
      "by poor dress.";

  // `sink` may be null -- the simulator and the golden tests have nowhere to
  // persist to, exactly as SettingsScreen's may be.
  //
  // AND `body` MAY BE NULL, which is a supported state and not an oversight: it
  // means the preview box is drawn empty, which is what a test that only checks
  // the view model wants. The same call PageMetrics::italic makes -- a
  // degradation, not a failure.
  //
  // NO BOOK TITLE. The band's right slot is empty because these settings are
  // device-wide; naming one book would contradict the footnote below it. The slot
  // is still reserved ON THE BOARD, because a band's height must not vary by
  // screen, but that is the board's business and not this screen's.
  TypographyScreen(const Settings& initial, SettingsSink* sink, const GlyphSource* body);

  ScreenId id() const override { return ScreenId::Typography; }
  Fidelity fidelity() const override { return Fidelity::Mono; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const TypographyViewModel& vm() const { return vm_; }
  const Settings& settings() const { return settings_; }

  // Which setting a row edits. The board's order, which is the only thing that
  // makes the table in the .cpp checkable against design/Typography.dc.html by
  // eye.
  enum class Field { Font, Size, Margins, LineSpacing, Alignment };

 private:
  // Cycles the focused row's value forward, wrapping. Commits.
  Action cycleFocused();
  // A ROW IS FOCUSABLE IFF IT HAS MORE THAN ONE VALUE, which is derived rather
  // than tabulated -- so `Font` becomes reachable the moment a second body face is
  // vendored, with no line to remember to change here. Consumed by FocusScreen
  // through Focus::Gate, the same path Settings' section headers take.
  bool focusable(int index) const override;
  void syncVm() override;
  int firstFocusable() const;
  // How many values the field on this row offers. 1 for Font while one face is
  // vendored, which is the single fact driving focusability above.
  static int valueCount(Field f);

  Settings settings_;
  SettingsSink* sink_;
  const GlyphSource* body_;
  TypographyViewModel vm_;
};

}  // namespace reader
