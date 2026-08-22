#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/screen_settings.h"
#include "reader/theme_quiet.h"

using reader::Button;
using reader::InputEvent;
using reader::PressKind;
using reader::Settings;
using reader::SettingsScreen;

namespace {

const InputEvent kDown{Button::Down, PressKind::Short};
const InputEvent kUp{Button::Up, PressKind::Short};
const InputEvent kChange{Button::Confirm, PressKind::Short};

// Records what was committed, and can refuse -- both halves matter. See
// SettingsSink: a refused write must still leave the new value on screen, because
// it HAS taken effect in RAM.
class RecordingSink : public reader::SettingsSink {
 public:
  bool commit(const Settings& s) override {
    ++commits;
    last = s;
    return !refuse;
  }
  int commits = 0;
  bool refuse = false;
  Settings last{};
};

// A screen sized as the real panel sizes it, so the visible window is the one the
// device shows rather than an arbitrary number.
SettingsScreen sized(const Settings& s, reader::SettingsSink* sink, int panelH = 800) {
  ramp::Ramp r;
  reader::QuietTheme theme;
  int listH = 0, rowH = 0, headerH = 0;
  theme.settingsMetrics(panelH, r.fonts, listH, rowH, headerH);
  SettingsScreen scr(s, sink);
  scr.setMetrics(listH, rowH, headerH);
  return scr;
}

// The label of whatever row is focused, or "" for nothing.
std::string focusedLabel(const SettingsScreen& scr) {
  const int f = scr.vm().focusedRow;
  if (f < 0 || f >= static_cast<int>(scr.vm().rows.size())) return "";
  return scr.vm().rows[static_cast<size_t>(f)].label;
}

std::string valueOf(const SettingsScreen& scr, const std::string& label) {
  for (const auto& row : scr.vm().rows)
    if (row.label == label) return row.value;
  return "<not visible>";
}

}  // namespace

TEST_CASE("focus starts on the first row with a setting behind it, not on row 0") {
  // Row 0 is the TYPOGRAPHY header and rows 1-5 are typography settings that do
  // not exist until Phase 3. Landing on any of them would be a focus the user
  // cannot act on.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(focusedLabel(scr) == "Sleep after");
}

TEST_CASE("focus skips headers and inert rows in both directions") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  REQUIRE(focusedLabel(scr) == "Sleep after");

  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
  // `Sleep screen` follows and is not focusable, so DOWN from here must not land
  // on it -- it wraps past it to the first focusable row instead. (It used to stop
  // here, which made Settings the one list in the firmware that did not roll over.)
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Sleep after");

  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Sleep after");
  // And UP from the first focusable row wraps to the last rather than climbing
  // into the DEVICE header or the typography rows above it.
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
}

TEST_CASE("every move on this list changes something, so every move repaints") {
  // This used to assert the opposite for UP at the first focusable row: the list
  // clamped, so the press changed nothing and had to return none() rather than
  // spend a 520 ms repaint drawing an identical screen. The list wraps now, so
  // there is no press that changes nothing -- which is the OTHER way of not
  // feeling like a stuck button.
  //
  // The none() branch has not gone; it is now the all-headers guard in moveFocus,
  // which kItems cannot currently reach. That is deliberately not faked here: a
  // test that constructed an unreachable table would pin the guard's shape rather
  // than the screen's behaviour.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(scr.onEvent(kUp).kind == reader::Action::Kind::Redraw);
  CHECK(scr.onEvent(kDown).kind == reader::Action::Kind::Redraw);
}

TEST_CASE("CHANGE cycles the focused setting and commits it") {
  RecordingSink sink;
  Settings s;
  s.sleepAfterMs = 5u * 60u * 1000u;
  SettingsScreen scr = sized(s, &sink);
  REQUIRE(focusedLabel(scr) == "Sleep after");
  REQUIRE(valueOf(scr, "Sleep after") == "5 MIN");

  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") == "10 MIN");
  CHECK(sink.commits == 1);
  CHECK(sink.last.sleepAfterMs == 10u * 60u * 1000u);
  // The other fields ride along untouched: commit() takes the whole struct, so a
  // screen that rebuilt it from its rows could silently reset one.
  CHECK(sink.last.fullOnTransition == s.fullOnTransition);
  CHECK(sink.last.fullRefreshEvery == s.fullRefreshEvery);
}

TEST_CASE("the cycle wraps, because one button has no way back") {
  RecordingSink sink;
  Settings s;
  s.sleepAfterMs = 30u * 60u * 1000u;  // the last step
  SettingsScreen scr = sized(s, &sink);
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") == "1 MIN");
}

TEST_CASE("the refresh cadence reads NEVER at zero, not EVERY 0 PAGES") {
  RecordingSink sink;
  Settings s;
  s.fullRefreshEvery = 0;
  SettingsScreen scr = sized(s, &sink);
  CHECK(valueOf(scr, "Full refresh") == "NEVER");
  scr.onEvent(kDown);
  REQUIRE(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Full refresh") == "EVERY 5 PAGES");
}

TEST_CASE("the transition toggle is ON/OFF and round-trips") {
  RecordingSink sink;
  Settings s;
  s.fullOnTransition = true;
  SettingsScreen scr = sized(s, &sink);
  scr.onEvent(kDown);
  scr.onEvent(kDown);
  REQUIRE(focusedLabel(scr) == "Refresh on screen change");
  CHECK(valueOf(scr, "Refresh on screen change") == "ON");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Refresh on screen change") == "OFF");
  CHECK(sink.last.fullOnTransition == false);
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Refresh on screen change") == "ON");
  CHECK(sink.last.fullOnTransition == true);
}

TEST_CASE("a REFUSED commit still shows the new value") {
  // The change has already happened in RAM and the device is already behaving the
  // new way. Reverting the display would make a read-only card look like a screen
  // that ignores its buttons -- and the shell is what reports the write failure.
  RecordingSink sink;
  sink.refuse = true;
  Settings s;
  s.fullOnTransition = true;
  SettingsScreen scr = sized(s, &sink);
  scr.onEvent(kDown);
  scr.onEvent(kDown);
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Refresh on screen change") == "OFF");
  CHECK(scr.settings().fullOnTransition == false);
}

TEST_CASE("CHANGE on a screen with no sink still edits, for the simulator") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  const std::string before = valueOf(scr, "Sleep after");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") != before);
}

TEST_CASE("a hand-edited value outside the cycle is escapable") {
  // The cycle is the only way to change this field, so a value that is not IN the
  // cycle must not be a dead end -- otherwise a settings.json edited to 7 minutes
  // could never be changed from the device.
  RecordingSink sink;
  Settings s;
  s.sleepAfterMs = 7u * 60u * 1000u;
  SettingsScreen scr = sized(s, &sink);
  REQUIRE(valueOf(scr, "Sleep after") == "7 MIN");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") != "7 MIN");
}

TEST_CASE("setFocus refuses a header or an inert row") {
  // A restored focus that landed on a header could not be moved off it in one
  // press, so the restore is refused and the focus stays where it was.
  SettingsScreen scr = sized(Settings{}, nullptr);
  const int wasFocus = scr.focus();
  CHECK_FALSE(scr.setFocus(0));  // TYPOGRAPHY
  CHECK_FALSE(scr.setFocus(1));  // Font, inert
  CHECK_FALSE(scr.setFocus(6));  // DEVICE
  CHECK_FALSE(scr.setFocus(999));
  CHECK(scr.focus() == wasFocus);
}

TEST_CASE("setFocus accepts a focusable row, and says whether anything moved") {
  // The bool is "something MOVED" here as it is everywhere else -- see
  // Screen::setFocus, which used to claim it meant "the restore landed" and
  // "the same contract ScrollWindow uses" in one sentence. So a focusable row
  // that is NOT the current one reports true, and asking for the row the screen
  // already sits on reports false while still being a perfectly good restore.
  SettingsScreen scr = sized(Settings{}, nullptr);
  REQUIRE(focusedLabel(scr) == "Sleep after");
  CHECK_FALSE(scr.setFocus(scr.focus()));
  CHECK(focusedLabel(scr) == "Sleep after");

  REQUIRE(scr.setFocus(8));
  REQUIRE(focusedLabel(scr) != "Sleep after");
  CHECK(scr.setFocus(7));
  CHECK(focusedLabel(scr) == "Sleep after");
}

TEST_CASE("the list FITS the panel, so no rail is drawn") {
  // Eleven items, all visible. It briefly did not fit -- adding the transition row
  // pushed it over and made it a scrolling list -- and then Wi-Fi was cut from V1
  // and CONNECTIONS went with it. Phase 3's typography settings will push it over
  // again, and this assertion is what will notice: renderSettings draws the rail
  // and takes its gutter off `totalRows > rows`, so the day this flips, the screen
  // starts scrolling without anything else changing.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(scr.vm().totalRows == 11);
  CHECK(static_cast<int>(scr.vm().rows.size()) == scr.vm().totalRows);
}

TEST_CASE("section headers are rows in the list, not decoration around it") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  int headers = 0;
  for (const auto& row : scr.vm().rows)
    if (row.isHeader) {
      ++headers;
      CHECK(row.value.empty());
      CHECK_FALSE(row.focusable);
    }
  CHECK(headers == 2);  // TYPOGRAPHY and DEVICE, and no CONNECTIONS any more
}

TEST_CASE("an inert row is marked unfocusable but is otherwise an ordinary row") {
  // The flag is about INPUT. It carries a label and a value exactly as a focusable
  // row does, so a theme has nothing to dim even if it wanted to.
  SettingsScreen scr = sized(Settings{}, nullptr);
  bool sawInert = false;
  for (const auto& row : scr.vm().rows) {
    if (row.isHeader || row.focusable) continue;
    sawInert = true;
    CHECK_FALSE(row.label.empty());
    CHECK_FALSE(row.value.empty());
  }
  CHECK(sawInert);
}
