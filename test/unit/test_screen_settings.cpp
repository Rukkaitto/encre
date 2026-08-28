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

TEST_CASE("focus starts on the first reachable row, not on row 0") {
  // Row 0 is the READING header. Row 1 is `Typography`, which is now the first row
  // with anything behind it -- the focus sat on `Sleep after` only while every row
  // above it was inert.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(focusedLabel(scr) == "Typography");
}

TEST_CASE("focus skips headers and inert rows in both directions") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  REQUIRE(focusedLabel(scr) == "Typography");

  // The DEVICE header sits between `Typography` and `Sleep after` and is stepped
  // straight over.
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Sleep after");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
  // `Sleep screen` follows and is not focusable, so DOWN from here must not land
  // on it -- it wraps past it to the first focusable row instead. (It used to stop
  // here, which made Settings the one list in the firmware that did not roll over.)
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Typography");

  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Sleep after");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Typography");
  // And UP from the first focusable row wraps to the last rather than climbing
  // into the READING header above it.
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
  // Down off `Typography`, which opens a screen rather than cycling a value.
  scr.onEvent(kDown);
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
  scr.onEvent(kDown);  // off `Typography`, onto `Sleep after`
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
  scr.onEvent(kDown);
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Refresh on screen change") == "OFF");
  CHECK(scr.settings().fullOnTransition == false);
}

TEST_CASE("CHANGE on a screen with no sink still edits, for the simulator") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  const std::string before = valueOf(scr, "Sleep after");
  scr.onEvent(kDown);  // off `Typography`, onto `Sleep after`
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
  scr.onEvent(kDown);  // off `Typography`, onto `Sleep after`
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") != "7 MIN");
}

TEST_CASE("setFocus refuses a header or an inert row") {
  // A restored focus that landed on a header could not be moved off it in one
  // press, so the restore is refused and the focus stays where it was.
  SettingsScreen scr = sized(Settings{}, nullptr);
  const int wasFocus = scr.focus();
  CHECK_FALSE(scr.setFocus(0));  // READING
  CHECK_FALSE(scr.setFocus(2));  // DEVICE
  CHECK_FALSE(scr.setFocus(6));  // Sleep screen, inert
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
  REQUIRE(focusedLabel(scr) == "Typography");
  CHECK_FALSE(scr.setFocus(scr.focus()));
  CHECK(focusedLabel(scr) == "Typography");

  REQUIRE(scr.setFocus(4));
  REQUIRE(focusedLabel(scr) != "Typography");
  CHECK(scr.setFocus(1));
  CHECK(focusedLabel(scr) == "Typography");
}

TEST_CASE("the list FITS the panel, so no rail is drawn") {
  // Seven items, all visible, where there were eleven. It briefly did not fit --
  // adding the transition row pushed it over and made it a scrolling list -- and
  // then Wi-Fi was cut from V1 and CONNECTIONS went with it; the five typography
  // readout rows becoming one door took four more. The reading settings still to
  // come will push it over again, and this assertion is what will notice:
  // renderSettings draws the rail and takes its gutter off `totalRows > rows`, so
  // the day this flips, the screen starts scrolling without anything else changing.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(scr.vm().totalRows == 7);
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
  CHECK(headers == 2);  // READING and DEVICE, and no CONNECTIONS any more
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


TEST_CASE("Settings' READING row opens the Typography panel") {
  // The one row on this screen that does not edit a value. It exists because the
  // typography settings now have a screen of their own that needs no open book, so
  // Settings can reach it -- and once there is a door, five rows that merely
  // displayed the values are redundant.
  SettingsScreen scr = sized(Settings{}, nullptr);

  REQUIRE(scr.vm().rows.size() == 7);
  CHECK(scr.vm().rows[0].label == "READING");
  CHECK(scr.vm().rows[0].isHeader);
  CHECK(scr.vm().rows[1].label == "Typography");
  // A CHEVRON AND NO VALUE: Home's menu rows state the rule -- a row states a
  // quantity or discloses a screen, never both.
  CHECK(scr.vm().rows[1].discloses);
  CHECK(scr.vm().rows[1].value.empty());
  CHECK(scr.vm().rows[1].focusable);
  CHECK(scr.vm().rows[2].label == "DEVICE");

  // THE FOCUS STARTS HERE. It sat on `Sleep after` only because every row above it
  // was inert.
  REQUIRE(focusedLabel(scr) == "Typography");

  const reader::Action a = scr.onEvent(kChange);
  CHECK(a.kind == reader::Action::Kind::Push);
  CHECK(a.target == reader::ScreenId::Typography);
}

TEST_CASE("the five old typography rows are gone") {
  // They were a readout nobody could act on. Asserted by ABSENCE, because a row left
  // behind would be drawn and unreachable forever and nothing else here would
  // notice -- the row count alone would still pass if one were swapped for another.
  SettingsScreen scr = sized(Settings{}, nullptr);
  for (const auto& r : scr.vm().rows) {
    CHECK(r.label != "Font");
    CHECK(r.label != "Size");
    CHECK(r.label != "Margins");
    CHECK(r.label != "Line spacing");
    CHECK(r.label != "Alignment");
    CHECK(r.label != "TYPOGRAPHY");
  }
}

TEST_CASE("the Confirm hint follows the focused row") {
  // THE FIRST HINT BAR HERE WHOSE TEXT VARIES WITHIN A SCREEN, and it has to:
  // screen_settings.cpp used to state the premise outright -- "CHANGE, not OPEN:
  // nothing here pushes a screen" -- and the READING row makes it false. A Confirm
  // labelled CHANGE that opened a screen is the misleading-button defect.
  SettingsScreen scr = sized(Settings{}, nullptr);
  REQUIRE(focusedLabel(scr) == "Typography");
  CHECK(scr.vm().hints[1] == "OPEN");

  // Down to the first DEVICE row, which cycles a value in place.
  scr.onEvent(kDown);
  REQUIRE(focusedLabel(scr) == "Sleep after");
  CHECK(scr.vm().hints[1] == "CHANGE");

  // And back, because a label that only ever moved one way would pass a one-press
  // test and leave the bar wrong for the rest of the session.
  scr.onEvent(kUp);
  REQUIRE(focusedLabel(scr) == "Typography");
  CHECK(scr.vm().hints[1] == "OPEN");

  // The other three slots never move: Back, Up and Down mean the same thing on
  // every row.
  CHECK(scr.vm().hints[0] == "BACK");
  CHECK(scr.vm().hints[2] == "UP");
  CHECK(scr.vm().hints[3] == "DOWN");
}

TEST_CASE("CHANGE on a device row still cycles, and OPEN does not") {
  // The two behaviours must not have leaked into each other: a disclosing row that
  // cycled a value, or a value row that pushed a screen, would each be a control
  // doing something other than what its hint says.
  RecordingSink sink;
  SettingsScreen scr = sized(Settings{}, &sink);
  const Settings before = scr.settings();

  REQUIRE(focusedLabel(scr) == "Typography");
  scr.onEvent(kChange);
  // THE WHOLE STRUCT, not the three device fields: `Settings` has a defaulted
  // operator== (settings.h), so this also covers the four typography fields a
  // field-by-field comparison would silently let a disclosing row change.
  CHECK(scr.settings() == before);
  // And nothing was persisted either -- a push that also committed would write the
  // file on every visit to the panel.
  CHECK(sink.commits == 0);

  scr.onEvent(kDown);
  REQUIRE(focusedLabel(scr) == "Sleep after");
  const reader::Action a = scr.onEvent(kChange);
  CHECK(a.kind == reader::Action::Kind::Redraw);  // not Push
  CHECK(scr.settings().sleepAfterMs != before.sleepAfterMs);
  CHECK(sink.commits == 1);
}
