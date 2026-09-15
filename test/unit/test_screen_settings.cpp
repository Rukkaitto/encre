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

// Presses DOWN until `label` is focused. A COUNT OF PRESSES WOULD BE A SECOND
// COPY OF THE ITEM TABLE: every case below used to spell one, so inserting the
// SLEEP SCREEN section moved eight hand-maintained numbers at once and a wrong
// one lands on a neighbouring row that cycles a different field. The walk is
// bounded so a label that is not reachable fails here rather than looping.
// Which rows a walk STEPS OVER is asserted by its own case above, not here.
void focusTo(SettingsScreen& scr, const std::string& label) {
  for (int i = 0; i < 40 && focusedLabel(scr) != label; ++i) scr.onEvent(kDown);
  REQUIRE(focusedLabel(scr) == label);
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

TEST_CASE("focus skips headers in both directions") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  REQUIRE(focusedLabel(scr) == "Typography");

  // The SLEEP SCREEN header sits between `Typography` and `Shows`, and the DEVICE
  // header between `Cover fit` and `Sleep after`. Both are stepped straight over.
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Shows");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Cover fit");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Sleep after");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
  // Past the CONNECTIONS header, which the gate steps over exactly as it steps
  // over the three above it.
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Wi-Fi");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Wallabag");
  // The last row of the list, so DOWN wraps to the first focusable row. (It used
  // to stop here, which made Settings the one list in the firmware that did not
  // roll over.)
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Typography");

  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Wallabag");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Wi-Fi");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Refresh on screen change");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Full refresh");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Sleep after");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Cover fit");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Shows");
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Typography");
  // And UP from the first focusable row wraps to the last rather than climbing
  // into the READING header above it.
  scr.onEvent(kUp);
  CHECK(focusedLabel(scr) == "Wallabag");
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
  // Off `Typography`, which opens a screen rather than cycling a value.
  focusTo(scr, "Sleep after");
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
  focusTo(scr, "Sleep after");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") == "1 MIN");
}

TEST_CASE("the refresh cadence reads NEVER at zero, not EVERY 0 PAGES") {
  RecordingSink sink;
  Settings s;
  s.fullRefreshEvery = 0;
  SettingsScreen scr = sized(s, &sink);
  CHECK(valueOf(scr, "Full refresh") == "NEVER");
  focusTo(scr, "Full refresh");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Full refresh") == "EVERY 5 PAGES");
}

TEST_CASE("the transition toggle is ON/OFF and round-trips") {
  RecordingSink sink;
  Settings s;
  s.fullOnTransition = true;
  SettingsScreen scr = sized(s, &sink);
  focusTo(scr, "Refresh on screen change");
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
  focusTo(scr, "Refresh on screen change");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Refresh on screen change") == "OFF");
  CHECK(scr.settings().fullOnTransition == false);
}

TEST_CASE("CHANGE on a screen with no sink still edits, for the simulator") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  const std::string before = valueOf(scr, "Sleep after");
  focusTo(scr, "Sleep after");
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
  focusTo(scr, "Sleep after");
  scr.onEvent(kChange);
  CHECK(valueOf(scr, "Sleep after") != "7 MIN");
}

TEST_CASE("setFocus refuses a header or an inert row") {
  // A restored focus that landed on a header could not be moved off it in one
  // press, so the restore is refused and the focus stays where it was.
  SettingsScreen scr = sized(Settings{}, nullptr);
  const int wasFocus = scr.focus();
  CHECK_FALSE(scr.setFocus(0));  // READING
  CHECK_FALSE(scr.setFocus(2));  // SLEEP SCREEN
  CHECK_FALSE(scr.setFocus(5));  // DEVICE
  CHECK(scr.focus() == wasFocus);
}

TEST_CASE("setFocus CLAMPS an out-of-range index rather than refusing it") {
  // This case used to be one line inside the one above, asserting that
  // setFocus(999) was refused -- and it passed for an ACCIDENTAL reason: `set()`
  // clamps (a record naming row 400 of a three-row list means "as far down as you
  // can go"), and the last item then happened to be the inert `Sleep screen`, so
  // the clamp landed somewhere the gate refused. The last item is `Wi-Fi` now,
  // which is focusable, so the clamp lands and the restore succeeds -- which is
  // what `set()` has always been specified to do. The last item is `wallabag`
  // now, and it is focusable for the same reason.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(scr.setFocus(999));
  CHECK(focusedLabel(scr) == "Wallabag");
}

TEST_CASE("setFocus refuses Cover fit while it is inert, and accepts it when it is not") {
  // The restore path, and the one row whose focusability is DERIVED. A session
  // record naming row 4 must not put the focus somewhere the user cannot move off
  // in one press -- the whole reason setFocus consults the gate.
  Settings s;
  s.sleepShows = reader::SleepShows::Details;
  SettingsScreen hidden = sized(s, nullptr);
  const int was = hidden.focus();
  CHECK_FALSE(hidden.setFocus(4));
  CHECK(hidden.focus() == was);

  s.sleepShows = reader::SleepShows::Cover;
  SettingsScreen shown = sized(s, nullptr);
  CHECK(shown.setFocus(4));
  CHECK(focusedLabel(shown) == "Cover fit");
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

  REQUIRE(scr.setFocus(6));
  REQUIRE(focusedLabel(scr) != "Typography");
  CHECK(scr.setFocus(1));
  CHECK(focusedLabel(scr) == "Typography");
}

TEST_CASE("the list FITS the panel, so no rail is drawn") {
  // NINE items, all visible, where there were seven and before that eleven. It
  // briefly did not fit -- adding the transition row pushed it over and made it a
  // scrolling list -- and then Wi-Fi was cut from V1 and CONNECTIONS went with it;
  // the five typography readout rows becoming one door took four more, and the
  // SLEEP SCREEN section has now put two back. The reading settings still to come
  // will push it over again, and this assertion is what will notice:
  // renderSettings draws the rail and takes its gutter off `totalRows > rows`, so
  // the day this flips, the screen starts scrolling without anything else changing.
  //
  // setMetrics counts from the TOP, which is the conservative end -- three of the
  // FOUR headers are in the first six items and the fourth is last, so the window
  // it counts is the tallest one the list has. `rows.size() == totalRows` is what
  // says every item still fits.
  SettingsScreen scr = sized(Settings{}, nullptr);
  CHECK(scr.vm().totalRows == 12);
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
  CHECK(headers == 4);  // READING, SLEEP SCREEN, DEVICE and CONNECTIONS
}

TEST_CASE("with the defaults, no row is drawn inert") {
  // `Sleep screen` / `BOOK COVER` was the last row here with nothing behind it
  // (issue #11) and it is gone -- the setting is real now and lives in SLEEP
  // SCREEN as two rows that act. So on a default card every drawn row responds.
  SettingsScreen scr = sized(Settings{}, nullptr);
  for (const auto& row : scr.vm().rows)
    if (!row.isHeader) CHECK(row.focusable);
}

TEST_CASE("an inert row is marked unfocusable but is otherwise an ordinary row") {
  // The flag is about INPUT. It carries a label and a value exactly as a focusable
  // row does, so a theme has nothing to dim even if it wanted to.
  //
  // `Cover fit` is the only row that can be inert now, and only while `Shows`
  // shows no cover -- so this case has to ASK for that state rather than find it.
  Settings s;
  s.sleepShows = reader::SleepShows::Details;
  SettingsScreen scr = sized(s, nullptr);
  bool sawInert = false;
  for (const auto& row : scr.vm().rows) {
    if (row.isHeader || row.focusable) continue;
    sawInert = true;
    CHECK(row.label == "Cover fit");
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

  REQUIRE(scr.vm().rows.size() == 12);
  CHECK(scr.vm().rows[0].label == "READING");
  CHECK(scr.vm().rows[0].isHeader);
  CHECK(scr.vm().rows[1].label == "Typography");
  // A CHEVRON AND NO VALUE: Home's menu rows state the rule -- a row states a
  // quantity or discloses a screen, never both.
  CHECK(scr.vm().rows[1].discloses);
  CHECK(scr.vm().rows[1].value.empty());
  CHECK(scr.vm().rows[1].focusable);
  CHECK(scr.vm().rows[2].label == "SLEEP SCREEN");

  // THE FOCUS STARTS HERE. It sat on `Sleep after` only because every row above it
  // was inert.
  REQUIRE(focusedLabel(scr) == "Typography");

  const reader::Action a = scr.onEvent(kChange);
  CHECK(a.kind == reader::Action::Kind::Push);
  CHECK(a.target == reader::ScreenId::Typography);
}

TEST_CASE("Settings' CONNECTIONS row opens the Wi-Fi screen") {
  // THE DOOR TO THE WHOLE V1.1 FLOW, and it shipped missing. The six Wi-Fi
  // screens landed with design/Settings.dc.html saying CONNECTIONS IS BACK and
  // kItems still at nine, so nothing on the device could reach WifiSettings --
  // every screen built, every golden passed, and the feature was unreachable.
  // Nothing in the suite could see it: a screen nobody pushes is a screen nobody
  // tests the pushing of, and `make compare` renders the BOARD beside the
  // firmware, so what it measured was Settings drifting away from a board that
  // was already right.
  //
  // Asserted through the ROW rather than through kItems, because what was wrong
  // was the table, and a test that read the table would have agreed with it.
  SettingsScreen scr = sized(Settings{}, nullptr);

  REQUIRE(scr.vm().rows.size() == 12);
  CHECK(scr.vm().rows[9].label == "CONNECTIONS");
  CHECK(scr.vm().rows[9].isHeader);
  CHECK(scr.vm().rows[10].label == "Wi-Fi");
  // A CHEVRON AND NO VALUE, `Typography`'s own rule: a row states a quantity or
  // discloses a screen, never both. `Wi-Fi . ON DEMAND` is the tempting shape
  // that forbids, and WifiSettings' own header band already carries that state.
  CHECK(scr.vm().rows[10].discloses);
  CHECK(scr.vm().rows[10].value.empty());
  CHECK(scr.vm().rows[10].focusable);

  // It is the LAST row, so DOWN from the bottom of DEVICE reaches it and DOWN
  // again wraps -- the walk above pins that; here it only has to be reachable.
  REQUIRE(scr.setFocus(10));
  REQUIRE(focusedLabel(scr) == "Wi-Fi");

  // THE HINT SAYS OPEN AND THE PRESS PUSHES, and both come from
  // disclosedScreen -- one spelling, so a row cannot promise OPEN and then cycle
  // a value it has not got. That is the drifting-condition defect this project
  // has shipped twice, both times as a dead button.
  CHECK(scr.vm().hints[1] == "OPEN");
  const reader::Action a = scr.onEvent(kChange);
  CHECK(a.kind == reader::Action::Kind::Push);
  CHECK(a.target == reader::ScreenId::WifiSettings);
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
  focusTo(scr, "Sleep after");
  CHECK(scr.vm().hints[1] == "CHANGE");

  // And back, because a label that only ever moved one way would pass a one-press
  // test and leave the bar wrong for the rest of the session.
  focusTo(scr, "Typography");
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

  focusTo(scr, "Sleep after");
  const reader::Action a = scr.onEvent(kChange);
  CHECK(a.kind == reader::Action::Kind::Redraw);  // not Push
  CHECK(scr.settings().sleepAfterMs != before.sleepAfterMs);
  CHECK(sink.commits == 1);
}

// --- The SLEEP SCREEN section (design/Settings.dc.html) -----------------------

TEST_CASE("the SLEEP SCREEN section is drawn where the board puts it") {
  SettingsScreen scr = sized(Settings{}, nullptr);
  REQUIRE(scr.vm().rows.size() == 12);
  CHECK(scr.vm().rows[2].label == "SLEEP SCREEN");
  CHECK(scr.vm().rows[2].isHeader);
  CHECK(scr.vm().rows[3].label == "Shows");
  CHECK(scr.vm().rows[4].label == "Cover fit");
  CHECK(scr.vm().rows[5].label == "DEVICE");
  CHECK(scr.vm().rows[5].isHeader);
  // Neither row discloses: both cycle a value in place, so neither draws a
  // chevron and both state a value. Home's rule -- a row states a quantity or
  // discloses a screen, never both.
  CHECK_FALSE(scr.vm().rows[3].discloses);
  CHECK_FALSE(scr.vm().rows[4].discloses);
  CHECK(scr.vm().rows[3].value == "COVER + DETAILS");
  CHECK(scr.vm().rows[4].value == "FILL");
}

TEST_CASE("Shows cycles three ways and wraps") {
  RecordingSink sink;
  SettingsScreen scr = sized(Settings{}, &sink);
  focusTo(scr, "Shows");
  const int at = scr.focus();
  REQUIRE(valueOf(scr, "Shows") == "COVER + DETAILS");

  scr.onEvent(kChange);
  CHECK(scr.settings().sleepShows == reader::SleepShows::Details);
  CHECK(valueOf(scr, "Shows") == "DETAILS");
  scr.onEvent(kChange);
  CHECK(scr.settings().sleepShows == reader::SleepShows::Cover);
  CHECK(valueOf(scr, "Shows") == "COVER");
  scr.onEvent(kChange);
  CHECK(scr.settings().sleepShows == reader::SleepShows::CoverAndDetails);  // wrapped
  CHECK(valueOf(scr, "Shows") == "COVER + DETAILS");
  CHECK(scr.focus() == at);  // cycling never moves the focus
  CHECK(sink.commits == 3);
}

TEST_CASE("Cover fit cycles two ways and wraps") {
  RecordingSink sink;
  SettingsScreen scr = sized(Settings{}, &sink);
  focusTo(scr, "Cover fit");
  REQUIRE(valueOf(scr, "Cover fit") == "FILL");
  scr.onEvent(kChange);
  CHECK(scr.settings().coverFit == reader::CoverFit::Whole);
  CHECK(valueOf(scr, "Cover fit") == "WHOLE");
  scr.onEvent(kChange);
  CHECK(scr.settings().coverFit == reader::CoverFit::Fill);
  CHECK(valueOf(scr, "Cover fit") == "FILL");
}

TEST_CASE("Cover fit is focusable only while Shows shows a cover") {
  // DERIVED, not tabulated -- Typography's precedent, where `Font` is unreachable
  // while one body face is vendored. A row that cannot act must not be selectable,
  // which is this screen's standing rule.
  for (const reader::SleepShows shows :
       {reader::SleepShows::Cover, reader::SleepShows::CoverAndDetails}) {
    Settings s;
    s.sleepShows = shows;
    SettingsScreen scr = sized(s, nullptr);
    bool reached = false;
    for (int i = 0; i < 40; ++i) {
      scr.onEvent(kDown);
      if (focusedLabel(scr) == "Cover fit") reached = true;
    }
    CHECK(reached);
  }

  Settings s;
  s.sleepShows = reader::SleepShows::Details;
  SettingsScreen hidden = sized(s, nullptr);
  bool landed = false;
  for (int i = 0; i < 40; ++i) {
    hidden.onEvent(kDown);
    if (focusedLabel(hidden) == "Cover fit") landed = true;
  }
  CHECK_FALSE(landed);
  // Still DRAWN, and still stating its value: the flag is about input, and an
  // inert row is drawn exactly as an unfocused focusable one.
  CHECK(valueOf(hidden, "Cover fit") == "FILL");
}

TEST_CASE("turning the cover off under the focus does not leave it stranded") {
  // `Shows` is the row ABOVE `Cover fit`, so a user can only reach this by being
  // on `Cover fit`, going up, and cycling to DETAILS. The focus is then on `Shows`
  // and the row below has gone inert -- which must not make DOWN land on it.
  SettingsScreen scr = sized(Settings{}, nullptr);
  focusTo(scr, "Shows");
  scr.onEvent(kChange);  // -> DETAILS
  REQUIRE(scr.settings().sleepShows == reader::SleepShows::Details);
  CHECK(focusedLabel(scr) == "Shows");
  scr.onEvent(kDown);
  CHECK(focusedLabel(scr) == "Sleep after");
}

TEST_CASE("the SLEEP SCREEN rows say CHANGE, not OPEN") {
  // One row on this screen discloses and the rest edit in place. Both new rows
  // edit, so the Confirm slot must read CHANGE on each -- and Activate must
  // answer a redraw rather than a push.
  //
  // `Cover fit` FIRST, and the order is load-bearing: cycling `Shows` reaches
  // DETAILS, which makes `Cover fit` inert -- so walking to it afterwards is a
  // walk to a row that is no longer there. The first draft of this case did
  // exactly that and failed, which is the behaviour working rather than a bug.
  SettingsScreen scr = sized(Settings{}, nullptr);
  focusTo(scr, "Cover fit");
  CHECK(scr.vm().hints[1] == "CHANGE");
  CHECK(scr.onEvent(kChange).kind == reader::Action::Kind::Redraw);
  focusTo(scr, "Shows");
  CHECK(scr.vm().hints[1] == "CHANGE");
  CHECK(scr.onEvent(kChange).kind == reader::Action::Kind::Redraw);
}

TEST_CASE("no row carries a placeholder any more") {
  // `Sleep screen` was the last row with a board placeholder and nothing behind
  // it -- the row CLAUDE.md ties to issue #11 by number. Item::placeholder is
  // removed outright, and this asserts the field is dead rather than assuming it,
  // exactly as ListRow::trackingEm1000 is handled: every drawn row either
  // discloses a screen or states a value that comes from `settings_`.
  //
  // Walked over every reachable state of the one setting that changes which rows
  // are inert, because an inert row is precisely where a placeholder used to live.
  for (const reader::SleepShows shows :
       {reader::SleepShows::Cover, reader::SleepShows::CoverAndDetails,
        reader::SleepShows::Details}) {
    Settings s;
    s.sleepShows = shows;
    SettingsScreen scr = sized(s, nullptr);
    for (const reader::SettingsRow& r : scr.vm().rows) {
      if (r.isHeader) continue;
      if (r.discloses) {
        CHECK(r.value.empty());
      } else {
        CHECK_FALSE(r.value.empty());
      }
    }
  }
}
