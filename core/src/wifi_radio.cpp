#include "reader/wifi_radio.h"

#include <algorithm>

namespace reader {
namespace {

// FROM esp_wifi_types.h, named here rather than included: core/ compiles for
// macOS and the ESP32 alike and may not reach an ESP-IDF header. The cost of
// the copy is stated in the header; what makes it safe is that these are
// PROTOCOL constants -- 802.11 reason codes and Espressif's own additions --
// rather than an API that moves.
constexpr int kAuthExpire = 2;             // WIFI_REASON_AUTH_EXPIRE
constexpr int kAuthLeave = 3;              // WIFI_REASON_AUTH_LEAVE
constexpr int kHandshakeTimeout4Way = 15;  // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
constexpr int kGroupKeyUpdateTimeout = 16; // WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT
constexpr int kIe8021xAuthFailed = 23;     // WIFI_REASON_802_1X_AUTH_FAILED
constexpr int kNoApFound = 201;            // WIFI_REASON_NO_AP_FOUND
constexpr int kAuthFail = 202;             // WIFI_REASON_AUTH_FAIL
constexpr int kHandshakeTimeout = 204;     // WIFI_REASON_HANDSHAKE_TIMEOUT
// THE REST OF THE NO_AP_FOUND FAMILY, and this table shipped without them.
// Espressif split "no AP found" into four codes and only the first was here,
// so all three of these fell through `default:` to Incomplete -- whose
// sentence says the network "took the password but never finished
// connecting", asserting an association that never happened. A WPA3-only
// router (210) told the reader it had accepted their password.
//
// Verified against the installed header rather than remembered:
// esp_wifi_types_generic.h:175-177.
constexpr int kNoApCompatibleSecurity = 210;  // ..._NO_AP_FOUND_W_COMPATIBLE_SECURITY
constexpr int kNoApAuthmodeThreshold = 211;   // ..._NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
constexpr int kNoApRssiThreshold = 212;       // ..._NO_AP_FOUND_IN_RSSI_THRESHOLD
// THE AP REFUSED THE ASSOCIATION *TEMPORARILY*, which is a different thing from
// refusing it: 802.11w's comeback mechanism has the AP answer with "try again in
// N", and the ESP32 allows exactly ONE comeback before giving up ("Association
// refused too many times, max allowed 1"). Seen on glass 2026-09-15 against a
// real router, which is why it is named rather than left to `default:`.
//
// IT STAYS `Incomplete` AND THAT IS CORRECT. The password was never offered, so
// NotFound would be false (the AP was heard) and BadPassword would be a lie on a
// screen whose three copy shapes exist precisely so that cannot happen.
constexpr int kAssocComebackTooLong = 208;  // WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG

}  // namespace

const char* wifiReasonName(int reason) {
  // NAMED FOR THE LOG, not for the glass: this never reaches a reader, and the
  // screen's three copy shapes come from wifiFailureFor below. It exists because
  // `vendor reason 208` cost a round trip to a header to read, and the next
  // person diagnosing a join should not pay that again -- the same argument the
  // `[i]` line and `screenName` already make.
  switch (reason) {
    case kWifiReasonNoAddress: return "no address (associated, DHCP never finished)";
    case kAuthExpire: return "AUTH_EXPIRE";
    case kAuthLeave: return "AUTH_LEAVE";
    case kHandshakeTimeout4Way: return "4WAY_HANDSHAKE_TIMEOUT (usually a wrong passphrase)";
    case kGroupKeyUpdateTimeout: return "GROUP_KEY_UPDATE_TIMEOUT";
    case kIe8021xAuthFailed: return "802_1X_AUTH_FAILED";
    case kNoApFound: return "NO_AP_FOUND";
    case kAuthFail: return "AUTH_FAIL";
    case kHandshakeTimeout: return "HANDSHAKE_TIMEOUT";
    case kAssocComebackTooLong:
      return "ASSOC_COMEBACK_TIME_TOO_LONG (the AP refused association for now)";
    case kNoApCompatibleSecurity: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case kNoApAuthmodeThreshold: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case kNoApRssiThreshold: return "NO_AP_FOUND_IN_RSSI_THRESHOLD (heard, too weak)";
    default: return "unnamed";
  }
}

JoinFailure wifiFailureFor(int reason) {
  switch (reason) {
    // THE AP WAS NOT THERE. These are the codes that mean it, and the only
    // ones that must not offer to edit the password -- the password is not
    // what went wrong.
    //
    // THIS SAID "the only code that means it" AND NAMED ONE OF FOUR. Espressif
    // split the family at 210-212, and those three reached `default:` and the
    // Incomplete sentence -- which claims the network took the password. So a
    // WPA3-only router, an AP below the configured authmode, and an AP below
    // the RSSI threshold each told the reader their password had been
    // accepted, on a screen whose three copy shapes exist precisely so that
    // cannot happen. An absent claim beats a false one, and this was a false
    // one.
    //
    // 212 (below the RSSI threshold) is the one worth reading twice: it means
    // the AP was HEARD and refused as too weak, which is exactly what the
    // NotFound sentence's "it may be out of range" says.
    case kNoApFound:
    case kNoApCompatibleSecurity:
    case kNoApAuthmodeThreshold:
    case kNoApRssiThreshold:
      return JoinFailure::NotFound;

    // 208 is deliberately NOT here: the AP was heard, so NotFound would be
    // false. It falls to Incomplete below, which is the honest shape.

    // THE CREDENTIAL WAS REJECTED. A WPA2 AP that dislikes the passphrase
    // times out the four-way handshake rather than saying so, which is why the
    // timeout codes are here and not under Incomplete: for a PSK network they
    // ARE the wrong-password signal.
    case kAuthExpire:
    case kAuthLeave:
    case kHandshakeTimeout4Way:
    case kGroupKeyUpdateTimeout:
    case kIe8021xAuthFailed:
    case kAuthFail:
    case kHandshakeTimeout:
      return JoinFailure::BadPassword;

    // EVERYTHING ELSE, INCLUDING kWifiReasonNoAddress AND CODES THIS BUILD HAS
    // NEVER HEARD OF. See the header: an unknown that degrades into a vaguer
    // true sentence is the safe direction.
    default:
      return JoinFailure::Incomplete;
  }
}

std::vector<ScanResult> rankScanResults(const std::vector<ScanResult>& seen) {
  std::vector<ScanResult> out;
  out.reserve(seen.size() < kMaxScanRows ? seen.size() : kMaxScanRows);
  for (const ScanResult& r : seen) {
    // A hidden network broadcasts a blank SSID and there is no flow to reach
    // one from, so a blank row would be a choice with nothing behind it.
    if (r.ssid.empty()) continue;
    bool merged = false;
    for (ScanResult& kept : out) {
      if (kept.ssid != r.ssid) continue;
      // The same network on two bands. Keep the stronger sighting -- both take
      // the same credential, so the weaker one is not a second choice.
      if (r.rssi > kept.rssi) {
        kept.rssi = r.rssi;
        kept.locked = r.locked;
      }
      merged = true;
      break;
    }
    if (!merged) out.push_back(r);
  }
  // STABLE, so two scans that saw the same networks at the same strengths
  // produce the same order and the rows do not shuffle under the focus.
  std::stable_sort(out.begin(), out.end(),
                   [](const ScanResult& a, const ScanResult& b) { return a.rssi > b.rssi; });
  if (out.size() > kMaxScanRows) out.resize(kMaxScanRows);
  return out;
}

}  // namespace reader
