#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

// THE RADIO, AS core/ SEES IT -- an interface with a fake, which is
// FileSystem's shape and SettingsSink's and CoverSource's argument.
//
// The deciding reason is not testability in the abstract. It is that two of
// WifiError's three shapes cannot otherwise be driven from the state they exist
// for: you cannot ask a real router to answer NO_AP_FOUND on demand, and
// Heap::install already exists in this project for exactly that -- injecting a
// failure the desktop cannot produce.
//
// IT IS POLL-SHAPED, NOT CALLBACK-SHAPED, AND THAT IS LOAD-BEARING. Arduino's
// Wi-Fi events arrive on the system event task, and shell/src/main.cpp creates
// NO TASKS AT ALL -- `xTaskCreate` appears zero times in it. A callback
// mutating App state from another task would be the riskiest thing in this
// feature, and it buys nothing: every slow job in this firmware already polls
// from loop()'s quiet window (the deferred page count, the grayscale
// refinement, the ring warm, the card-log flush, the position save).
struct ScanResult {
  std::string ssid;
  int rssi = 0;         // dBm, negative; closer to zero is stronger
  bool locked = false;  // needs a passphrase
};

enum class ScanState { Idle, Running, Done, Failed };
enum class JoinState { Idle, Running, Ok, Failed };

// The three shapes WifiError draws. A join fails three distinguishable ways and
// one sentence would be a lie -- BookError's argument, and the reason that
// screen is three boards.
enum class JoinFailure {
  BadPassword,  // the AP rejected the credential
  NotFound,     // the AP never answered: out of range, off, or a stale scan
  Incomplete,   // associated and never finished -- DHCP, and everything else
};

// WHAT THE SHELL PASSES WHEN ASSOCIATION SUCCEEDED AND NO ADDRESS ARRIVED.
// DHCP timing out is not a WIFI_REASON_* at all, so it needs a value of its
// own, and a negative one cannot collide with the vendor's unsigned codes.
inline constexpr int kWifiReasonNoAddress = -1;

// MAPS A VENDOR REASON CODE ONTO ONE OF THE THREE, and it lives in core/ beside
// the enum for bookErrorReasonFor's reason: a ladder in shell/ grows a fourth
// case nobody remembers to add, in the one directory where nothing executes it,
// and a wrong mapping here tells somebody their password was rejected by a
// router that is not there.
//
// THE COST IS STATED: core/ carries a handful of integer constants that came
// from esp_wifi_types.h. That is a device dependency in the portable layer, and
// it is the smaller of the two edges -- the alternative is a switch the desktop
// suite cannot reach.
//
// UNKNOWN CODES BECOME Incomplete, deliberately. "The connection didn't finish"
// is true of any failure past association and claims nothing specific, where
// routing an unrecognised code into one of the other two asserts a cause nobody
// established. An unknown that degrades into a vaguer TRUE sentence is the safe
// direction.
JoinFailure wifiFailureFor(int reason);

// The picker shows at most this many. A scan in a block of flats returns
// thirty; Rescan is the last row, so the cap is what keeps it reachable in a
// couple of held presses.
inline constexpr size_t kMaxScanRows = 20;

// WHAT THE PICKER ACTUALLY LISTS, from what the radio actually saw.
//
//   - EMPTY SSIDs ARE DROPPED. A hidden network broadcasts a blank one, and
//     there is no join-hidden flow to reach it from -- a blank row is a choice
//     with nothing behind it.
//   - DEDUPLICATED BY SSID, KEEPING THE STRONGEST. A dual-band router answers a
//     scan twice and both bands take the same credential, so two identical rows
//     would be a choice with no meaning.
//   - SORTED BY SIGNAL, DESCENDING, so the network you are standing next to is
//     first. Ties keep the order the radio reported, which is what stops rows
//     shuffling under the focus between two scans that saw the same thing.
//   - CAPPED at kMaxScanRows.
//
// A free function rather than a method on the radio: it is a decision about
// what to draw, it is the part with edges worth testing, and the fake and the
// real radio must not be able to answer it differently.
std::vector<ScanResult> rankScanResults(const std::vector<ScanResult>& seen);

class WifiRadio {
 public:
  virtual ~WifiRadio() = default;

  // Starts a scan. False means the radio would not come up at all, which the
  // caller shows as an empty picker rather than a join failure.
  virtual bool beginScan() = 0;
  virtual ScanState scanState() const = 0;
  // Valid once scanState() is Done. Raw, in the order the radio reported --
  // rankScanResults is what turns it into rows.
  virtual const std::vector<ScanResult>& scanResults() const = 0;

  // Starts a join. `psk` is empty for an open network. False means the attempt
  // could not be started; a started attempt reports through joinState().
  virtual bool beginJoin(std::string_view ssid, std::string_view psk) = 0;
  virtual JoinState joinState() const = 0;
  // The vendor reason, valid once joinState() is Failed. Pass it through
  // wifiFailureFor rather than switching on it at the call site.
  virtual int joinReason() const = 0;

  // Takes the radio down. Called after READY and after every failure, because
  // Wi-Fi stays off except while it is being used -- spec 3.3, and on this
  // hardware it is forced rather than chosen: the static allocation alone is
  // ~23 KB against a measured 13,696-byte floor with a book open.
  virtual void down() = 0;
};

}  // namespace reader
