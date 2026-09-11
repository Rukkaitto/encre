// THE TWO DECISIONS ON THE RADIO'S EDGE: which of WifiError's three shapes a
// vendor reason code means, and which rows a scan turns into. Both are free
// functions in core/ so the desktop can reach them -- a ladder in shell/ grows
// a fourth case nobody adds, in the one directory where nothing executes it.
#include <initializer_list>
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_wifi_radio.h"
#include "reader/wifi_radio.h"

using namespace reader;

namespace {
ScanResult ap(std::string ssid, int rssi, bool locked = true) {
  ScanResult r;
  r.ssid = std::move(ssid);
  r.rssi = rssi;
  r.locked = locked;
  return r;
}
}  // namespace

// ------------------------------------------------------------ reason mapping

TEST_CASE("the whole NO_AP_FOUND FAMILY means the network was not there") {
  // The one shape whose board drops the EDIT PASSWORD slab, so the codes that
  // must not be reached by accident -- and the codes that must not be MISSED,
  // which is what this case was blind to.
  //
  // IT ASSERTED "only NO_AP_FOUND" AND CHECKED ONE OF FOUR. Espressif split
  // the family at 210-212 and those three fell through to Incomplete, whose
  // sentence says the network "took the password but never finished
  // connecting" -- an association that never happened. A WPA3-only router
  // (210) told the reader it had accepted their password, on the one screen
  // whose three copy shapes exist so that cannot happen.
  //
  // The codes are verified against the installed header rather than
  // remembered: esp_wifi_types_generic.h:175-177.
  for (const int found : {201, 210, 211, 212}) {
    CAPTURE(found);
    CHECK(wifiFailureFor(found) == JoinFailure::NotFound);
  }
  // 205 and 209 are the NEIGHBOURS, and they are in the exclusion list for
  // that reason: a fix that routed "anything in the 2xx range" to NotFound
  // would pass the loop above and fail here.
  for (const int other : {2, 3, 15, 16, 23, 202, 204, 205, 209, 213, 0, 99, -1}) {
    CAPTURE(other);
    CHECK(wifiFailureFor(other) != JoinFailure::NotFound);
  }
}

TEST_CASE("THE HANDSHAKE TIMEOUTS ARE A WRONG PASSWORD, NOT AN INCOMPLETE JOIN") {
  // A WPA2 AP that dislikes the passphrase times the four-way handshake out
  // rather than saying so, so for a PSK network these ARE the wrong-password
  // signal. Reading them as "didn't finish" would send the reader to a dialog
  // with no EDIT PASSWORD on it, which is the one thing they need.
  for (const int reason : {2, 3, 15, 16, 23, 202, 204}) {
    CAPTURE(reason);
    CHECK(wifiFailureFor(reason) == JoinFailure::BadPassword);
  }
}

TEST_CASE("an unknown code degrades into the vaguer TRUE sentence") {
  // "The connection didn't finish" is true of any failure past association and
  // claims nothing specific. Routing an unrecognised code into one of the
  // other two would assert a cause nobody established.
  for (const int reason : {0, 1, 5, 8, 39, 200, 203, 205, 209, 213, 1000, -7}) {
    CAPTURE(reason);
    CHECK(wifiFailureFor(reason) == JoinFailure::Incomplete);
  }
  // And the value the shell passes when association worked and DHCP did not.
  CHECK(wifiFailureFor(kWifiReasonNoAddress) == JoinFailure::Incomplete);
}

// ----------------------------------------------------------- scan to picker

TEST_CASE("the strongest network is first") {
  const std::vector<ScanResult> rows =
      rankScanResults({ap("WEAK", -85), ap("STRONG", -35), ap("MIDDLING", -60)});
  REQUIRE(rows.size() == 3);
  CHECK(rows[0].ssid == "STRONG");
  CHECK(rows[1].ssid == "MIDDLING");
  CHECK(rows[2].ssid == "WEAK");
}

TEST_CASE("A DUAL-BAND ROUTER IS ONE ROW, AT ITS STRONGER SIGHTING") {
  // Both bands take the same credential, so two identical rows would be a
  // choice with no meaning.
  const std::vector<ScanResult> rows =
      rankScanResults({ap("HOME", -70), ap("OTHER", -80), ap("HOME", -40)});
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].ssid == "HOME");
  CHECK(rows[0].rssi == -40);
  CHECK(rows[1].ssid == "OTHER");
}

TEST_CASE("the merged row keeps the stronger sighting's lock state") {
  // The two bands should agree, and if they do not, the one being shown is the
  // one whose signal the row is reporting.
  const std::vector<ScanResult> rows =
      rankScanResults({ap("HOME", -80, true), ap("HOME", -30, false)});
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].rssi == -30);
  CHECK_FALSE(rows[0].locked);
}

TEST_CASE("a hidden network's blank SSID is dropped rather than drawn") {
  // There is no join-hidden flow, so a blank row is a choice with nothing
  // behind it.
  const std::vector<ScanResult> rows = rankScanResults({ap("", -30), ap("REAL", -60), ap("", -40)});
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].ssid == "REAL");
}

TEST_CASE("the list is capped, and the cap keeps the STRONGEST") {
  // Rescan is the last row, so the cap is what keeps it reachable in a couple
  // of held presses -- and dropping the weakest is the only cut that does not
  // hide the network you are standing next to.
  std::vector<ScanResult> seen;
  for (size_t i = 0; i < kMaxScanRows + 12; ++i) {
    seen.push_back(ap("N" + std::to_string(i), -100 + static_cast<int>(i)));
  }
  const std::vector<ScanResult> rows = rankScanResults(seen);
  REQUIRE(rows.size() == kMaxScanRows);
  CHECK(rows[0].rssi == -100 + static_cast<int>(kMaxScanRows + 11));
  for (const ScanResult& r : rows) CHECK(r.rssi > -100 + 11);
}

TEST_CASE("EQUAL SIGNALS KEEP THE RADIO'S ORDER, so rows do not shuffle between scans") {
  // An unstable sort would reorder equal-strength rows from one rescan to the
  // next, moving the row under the focus for no reason the reader can see --
  // and with a cap on top it would change WHICH networks survive.
  //
  // THE FIXTURE'S SHAPE IS THE WHOLE POINT AND TOOK TWO ATTEMPTS TO GET RIGHT.
  // Four equal entries failed to catch a std::sort mutation, because every
  // standard library falls back to insertion sort below a threshold. Sixty
  // equal entries failed too, for a subtler reason: an all-equal range is
  // ALREADY SORTED by this comparator, and libc++'s introsort has a
  // nearly-sorted fast path that leaves it alone. What bites is a large,
  // genuinely disordered range with ties inside it, which is also what a real
  // scan in a dense building looks like.
  std::vector<ScanResult> seen;
  for (int i = 0; i < 200; ++i) {
    // Five signal levels interleaved, so the input is far from sorted and each
    // level has forty members -- twice the cap.
    seen.push_back(ap("N" + std::to_string(i), -30 - (i % 5) * 10));
  }
  std::vector<std::string> expected;  // the first twenty of the strongest level
  for (int i = 0; i < 200 && expected.size() < kMaxScanRows; ++i) {
    if (i % 5 == 0) expected.push_back("N" + std::to_string(i));
  }
  for (int pass = 0; pass < 3; ++pass) {
    const std::vector<ScanResult> rows = rankScanResults(seen);
    REQUIRE(rows.size() == kMaxScanRows);
    for (size_t i = 0; i < rows.size(); ++i) {
      CAPTURE(i);
      CHECK(rows[i].rssi == -30);
      CHECK(rows[i].ssid == expected[i]);
    }
  }
}

TEST_CASE("an empty scan is an empty list, not a crash") {
  CHECK(rankScanResults({}).empty());
  CHECK(rankScanResults({ap("", -30)}).empty());
}

// ------------------------------------------------------------------ the fake

TEST_CASE("the fake reports nothing until a test says the scan finished") {
  // Poll-shaped, and deliberately not self-advancing: that is what lets a case
  // pin what a press arriving mid-scan does.
  FakeWifiRadio radio;
  radio.seen = {ap("HOME", -40)};
  CHECK(radio.scanState() == ScanState::Idle);
  REQUIRE(radio.beginScan());
  CHECK(radio.scanState() == ScanState::Running);
  CHECK(radio.scanResults().empty());
  radio.finishScan();
  CHECK(radio.scanState() == ScanState::Done);
  REQUIRE(radio.scanResults().size() == 1);
  CHECK(radio.scansStarted == 1);
}

TEST_CASE("the fake can refuse to come up at all") {
  FakeWifiRadio radio;
  radio.refuseScan = true;
  CHECK_FALSE(radio.beginScan());
  CHECK(radio.scansStarted == 0);
  radio.refuseJoin = true;
  CHECK_FALSE(radio.beginJoin("HOME", "pw"));
  CHECK(radio.joinsStarted == 0);
}

TEST_CASE("every failure shape is reachable from the fake, which is the point") {
  struct Case {
    int reason;
    JoinFailure shape;
  };
  for (const Case c : {Case{202, JoinFailure::BadPassword}, Case{201, JoinFailure::NotFound},
                       Case{kWifiReasonNoAddress, JoinFailure::Incomplete}}) {
    FakeWifiRadio radio;
    REQUIRE(radio.beginJoin("PENDRAGON", "correcthorse"));
    CHECK(radio.lastSsid == "PENDRAGON");
    CHECK(radio.lastPsk == "correcthorse");
    CHECK(radio.joinState() == JoinState::Running);
    radio.failJoin(c.reason);
    CHECK(radio.joinState() == JoinState::Failed);
    CHECK(wifiFailureFor(radio.joinReason()) == c.shape);
  }
}

TEST_CASE("an open network joins with no passphrase") {
  FakeWifiRadio radio;
  REQUIRE(radio.beginJoin("BUREAU-GUEST", ""));
  CHECK(radio.lastPsk.empty());
  radio.succeedJoin();
  CHECK(radio.joinState() == JoinState::Ok);
}

TEST_CASE("taking the radio down resets it, so the next action starts clean") {
  FakeWifiRadio radio;
  radio.seen = {ap("HOME", -40)};
  REQUIRE(radio.beginScan());
  radio.finishScan();
  REQUIRE(radio.beginJoin("HOME", "pw"));
  radio.succeedJoin();
  radio.down();
  CHECK(radio.downCalls == 1);
  CHECK(radio.scanState() == ScanState::Idle);
  CHECK(radio.joinState() == JoinState::Idle);
}
