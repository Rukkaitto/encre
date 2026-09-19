// THE PICKER AND THE HUB, whose behaviour was entirely unasserted. Between
// them they had a golden apiece and nothing else, so an adversarial mutation
// pass walked straight through: `setResults` keeping a stale focus, the whole
// scanning-owns-the-screen branch, SELECT not toggling, the sink never
// committing, the hold firing on SETUP, and -- the sharpest -- Activate on the
// SETUP row not pushing the picker AT ALL, which is the flow's own entry
// point.
//
// That is the pattern the pass found across this whole feature: everything the
// goldens DRAW is defended per pixel, almost nothing the screens DO.
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/screen_wifi_picker.h"
#include "reader/screen_wifi_settings.h"

using namespace reader;

namespace {

ScanResult ap(std::string ssid, int rssi, bool locked = true) {
  ScanResult r;
  r.ssid = std::move(ssid);
  r.rssi = rssi;
  r.locked = locked;
  return r;
}

const GestureEvent kConfirm{Gesture::Activate, 1, false};
const GestureEvent kBack{Gesture::Back, 1, false};
const GestureEvent kHold{Gesture::Secondary, 1, false};
const GestureEvent kNext{Gesture::Next, 1, false};
const GestureEvent kPrev{Gesture::Prev, 1, false};

// Records what the screen asked to persist, so "applied" and "persisted" can
// be told apart -- SettingsSink's own distinction, and the reason `commit`
// does both jobs under one name.
struct RecordingSink : WifiSink {
  int commits = 0;
  SavedNetworks last;
  bool commit(const SavedNetworks& n) override {
    ++commits;
    last = n;
    return true;
  }
};

}  // namespace

// ------------------------------------------------------------------ picker

TEST_CASE("a chosen network reports its name AND whether it is locked") {
  // `chosenLocked_` decides which screen comes next -- an open network joins
  // directly and a locked one asks for a password, which is the board's own
  // note -- so a picker that always reported `false` would send every reader
  // of a locked network straight into a join that cannot work, and one that
  // always reported `true` would put a keyboard in front of an open network
  // that needs none. Never set at all: 0 failures.
  WifiPickerScreen s({ap("LOCKED", -40, true), ap("OPEN", -50, false)}, 7);

  s.setFocus(0);
  REQUIRE(s.focus() == 0);
  s.onGesture(kConfirm);
  CHECK(s.chosenSsid() == "LOCKED");
  CHECK(s.chosenLocked());

  s.clearChoice();
  CHECK(s.chosenSsid().empty());
  CHECK_FALSE(s.chosenLocked());

  s.setFocus(1);
  REQUIRE(s.focus() == 1);
  s.onGesture(kConfirm);
  CHECK(s.chosenSsid() == "OPEN");
  CHECK_FALSE(s.chosenLocked());
}

TEST_CASE("a scan in flight owns the screen") {
  // The list is not there to move around in and the bar promises nothing but
  // Back. The whole branch could be deleted with the suite green, because
  // nothing called setScanning at all -- so neither the early return nor the
  // theme's drawStatusBar path was ever exercised.
  WifiPickerScreen s({ap("HOME", -40), ap("CAFE", -60)}, 7);
  REQUIRE(s.focus() == 0);

  CHECK(s.setScanning(true));
  // Asking for the state it is already in changes nothing and owes no repaint.
  CHECK_FALSE(s.setScanning(true));

  CHECK(s.onGesture(kNext).kind == Action::Kind::None);
  CHECK(s.focus() == 0);
  CHECK(s.onGesture(kConfirm).kind == Action::Kind::None);
  CHECK(s.chosenSsid().empty());
  CHECK_FALSE(s.rescanChosen());
  // BACK IS THE ONE THING THAT STILL WORKS, which is what keeps a scan that
  // never comes back from trapping the reader.
  CHECK(s.onGesture(kBack).kind == Action::Kind::Pop);

  CHECK(s.setScanning(false));
  CHECK(s.onGesture(kNext).kind == Action::Kind::Redraw);
}

TEST_CASE("a scan IN FLIGHT is not a scan that found nothing") {
  // TWO DIFFERENT STATES, and design/WifiPickerEmpty.dc.html says so in its
  // own first paragraph: "The scan completed and found nothing ... NOT THE
  // SCANNING STATE, which is a different thing and is drawStatusBar". The
  // board drew the distinction and `nothingFound = results_.empty()` did not
  // keep it, so the moment the picker opened -- before the radio had
  // answered -- it drew the empty copy OVER a status bar simultaneously
  // saying SCANNING. Reported off the device twice.
  WifiPickerScreen s({}, 7);
  // Not yet scanning and nothing found: that IS the empty state.
  CHECK(s.vm().nothingFound);

  REQUIRE(s.setScanning(true));
  CHECK(s.vm().scanning);
  // THE COPY MUST GO WHILE THE SCAN IS RUNNING. This is the assertion the
  // defect failed.
  CHECK_FALSE(s.vm().nothingFound);

  // A scan that comes back with nothing puts it back -- which is the state
  // the board is for.
  s.setResults({});
  CHECK_FALSE(s.vm().scanning);
  CHECK(s.vm().nothingFound);

  // And a scan that finds something has neither.
  REQUIRE(s.setScanning(true));
  s.setResults({ap("HOME", -40)});
  CHECK_FALSE(s.vm().scanning);
  CHECK_FALSE(s.vm().nothingFound);
}

TEST_CASE("a finished scan puts the focus back at the top") {
  // The old focus indexed a list that no longer exists. Keeping it lands the
  // reader on whatever now occupies that row -- a different network, under a
  // selection they did not move.
  WifiPickerScreen s({ap("A", -40), ap("B", -50), ap("C", -60)}, 7);
  s.setFocus(2);
  REQUIRE(s.focus() == 2);
  REQUIRE(s.vm().rows[2].ssid == "C");

  s.setResults({ap("Z", -30), ap("Y", -70)});
  CHECK(s.focus() == 0);
  REQUIRE(s.vm().rows.size() >= 1);
  CHECK(s.vm().rows[0].ssid == "Z");
  // And the Rescan row moved with the list's length rather than staying where
  // the old one put it.
  CHECK(s.vm().totalRows == 3);  // two networks plus Rescan
  CHECK(s.vm().rows.back().isRescan);
}

TEST_CASE("an empty scan is still a screen you can leave and rescan from") {
  // The empty variant keeps ONE live row, which is what the board draws --
  // and the two movers go quiet, because there is nothing to move between.
  WifiPickerScreen s({}, 7);
  REQUIRE(s.vm().totalRows == 1);
  REQUIRE(s.vm().rows.size() == 1);
  CHECK(s.vm().rows[0].isRescan);
  CHECK(s.vm().hints[1] == "RESCAN");
  CHECK(s.vm().hints[2].empty());
  CHECK(s.vm().hints[3].empty());

  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::Wifi);
  CHECK(s.rescanChosen());
  CHECK(s.chosenSsid().empty());
}

// --------------------------------------------------------------------- hub

TEST_CASE("SELECT toggles AUTO in place and persists it") {
  // Settings' CHANGE idiom: the row acts rather than disclosing. Both halves
  // survived separately -- the toggle not happening, and the sink never being
  // told -- and they are different failures: one is a screen that ignores its
  // button, the other is a preference that forgets itself at the next boot.
  SavedNetworks nets;
  REQUIRE(nets.remember("HOME", true) == Remembered::Yes);
  REQUIRE(nets.remember("BUREAU", true) == Remembered::Yes);
  RecordingSink sink;
  WifiSettingsScreen s(nets, &sink);

  // Row 0 is the SAVED NETWORKS header, so the focus starts on row 1.
  REQUIRE(s.focusedSsid() == "HOME");
  REQUIRE(s.vm().rows[1].value == "AUTO");

  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::Redraw);
  CHECK(s.vm().rows[1].value == "SAVED");
  CHECK(sink.commits == 1);
  CHECK(sink.last.automatic() == nullptr);

  // And the focus did not move under the reader: the list is rebuilt, which
  // is what redraws the row that LOST the flag, but the selection is kept.
  CHECK(s.focusedSsid() == "HOME");

  s.onGesture(kNext);
  REQUIRE(s.focusedSsid() == "BUREAU");
  s.onGesture(kConfirm);
  CHECK(s.vm().rows[2].value == "AUTO");
  CHECK(sink.commits == 2);
  REQUIRE(sink.last.automatic() != nullptr);
  CHECK(sink.last.automatic()->ssid == "BUREAU");
}

TEST_CASE("the SETUP row opens the picker, which is the flow's entry point") {
  // THE SHARPEST SURVIVOR: Activate on this row not pushing WifiPicker failed
  // nothing. It is the only way into a join from a device that has never seen
  // a network, so with it broken the whole feature is a list you cannot add
  // to -- and the empty variant, whose entire purpose is this one live row,
  // would have been a dead end.
  SavedNetworks nets;
  REQUIRE(nets.remember("HOME", true) == Remembered::Yes);
  WifiSettingsScreen s(nets, nullptr);

  // Down past the network row onto SETUP's `Join another network...`.
  s.onGesture(kNext);
  REQUIRE(s.focusedSsid().empty());  // not a network row
  CHECK(s.vm().hints[1] == "OPEN");

  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::WifiPicker);
}

TEST_CASE("the empty hub still reaches the picker") {
  // The variant exists so a device with nothing saved is not a dead end.
  WifiSettingsScreen s(SavedNetworks{}, nullptr);
  CHECK(s.vm().nothingSaved);
  REQUIRE(s.focusedSsid().empty());
  const Action a = s.onGesture(kConfirm);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::WifiPicker);
}

TEST_CASE("the hold opens the actions overlay on a network and does nothing on SETUP") {
  // The ring binds FORGET and there is nothing to forget from SETUP -- a bar
  // promising a hold with nothing behind it is the dead affordance the
  // four-slot rule exists to prevent. The ring and the binding read ONE
  // field, so they cannot disagree; this asserts they actually agree.
  SavedNetworks nets;
  REQUIRE(nets.remember("HOME", true) == Remembered::Yes);
  WifiSettingsScreen s(nets, nullptr);

  REQUIRE(s.focusedSsid() == "HOME");
  CHECK(s.vm().holds[1]);
  const Action a = s.onGesture(kHold);
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::WifiNetworkActions);

  s.onGesture(kNext);
  REQUIRE(s.focusedSsid().empty());
  // NO RING, so nothing is promised...
  CHECK_FALSE(s.vm().holds[1]);
  // ...and nothing happens, which is the half a drifting second condition
  // would break.
  CHECK(s.onGesture(kHold).kind == Action::Kind::None);
}

TEST_CASE("the empty hub promises no hold at all") {
  WifiSettingsScreen s(SavedNetworks{}, nullptr);
  CHECK_FALSE(s.vm().holds[1]);
  CHECK(s.onGesture(kHold).kind == Action::Kind::None);
  CHECK(s.longPressable() == 0);
}

TEST_CASE("the focus skips the headers in both directions") {
  SavedNetworks nets;
  REQUIRE(nets.remember("HOME", true) == Remembered::Yes);
  REQUIRE(nets.remember("BUREAU", true) == Remembered::Yes);
  WifiSettingsScreen s(nets, nullptr);
  // SAVED NETWORKS / HOME / BUREAU / SETUP / Join another network...
  REQUIRE(s.vm().rows.size() == 5);
  REQUIRE(s.vm().rows[0].isHeader);
  REQUIRE(s.vm().rows[3].isHeader);

  CHECK(s.focus() == 1);
  s.onGesture(kNext);
  CHECK(s.focus() == 2);
  s.onGesture(kNext);
  CHECK(s.focus() == 4);  // over the SETUP header
  s.onGesture(kNext);
  CHECK(s.focus() == 1);  // wraps, skipping the first header
  s.onGesture(kPrev);
  CHECK(s.focus() == 4);
  s.onGesture(kPrev);
  CHECK(s.focus() == 2);  // back over SETUP
}
