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

}  // namespace

JoinFailure wifiFailureFor(int reason) {
  switch (reason) {
    // THE AP WAS NOT THERE. The only code that means it, and the only one that
    // must not offer to edit the password -- the password is not what went
    // wrong.
    case kNoApFound:
      return JoinFailure::NotFound;

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
