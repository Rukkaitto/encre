#include "wifi_radio_arduino.h"

#include <Arduino.h>
#include <WiFi.h>

#include <algorithm>

namespace {

// HOW LONG A JOIN MAY TAKE BEFORE WE CALL IT, and it needs a number because
// Arduino's WiFi.status() has no "gave up" state for the case that matters
// most: an AP that is simply not there leaves it at WL_IDLE_STATUS or
// WL_DISCONNECTED indefinitely, so the CONNECTING... dialog would sit on the
// glass for ever.
//
// 15 s covers association plus a DHCP lease on a slow domestic router with
// room to spare, and it is the number the CONNECTING... copy was written
// against -- design/WifiConnect.dc.html says a join "takes an unknown one to
// ten seconds", which is where the ticker was cut.
constexpr uint32_t kJoinTimeoutMs = 15000;

// WHY A GLOBAL AND NOT A MEMBER: this is written from the SYSTEM EVENT TASK
// and read from loop(). It is a word-sized scalar and nothing else crosses
// the boundary -- no App state, no allocation, no container. That is
// deliberately narrower than what reader/wifi_radio.h warns about, which is a
// callback MUTATING App state from another task; recording an integer is not
// that, and it is the only way to get the vendor reason code at all.
//
// THE ARDUINO API HAS NO GETTER FOR IT. `WiFi.status()` collapses every
// failure into WL_NO_SSID_AVAIL / WL_CONNECT_FAILED / WL_DISCONNECTED, which
// cannot tell a wrong password from an AP whose security this device cannot
// meet -- and telling those apart is the entire reason WifiError has three
// shapes and wifiFailureFor exists. Without this the flow would be back to
// one sentence, and one sentence would be a lie.
volatile int gLastDisconnectReason = 0;
volatile bool gSawDisconnect = false;
volatile bool gGotAddress = false;

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      gLastDisconnectReason = static_cast<int>(info.wifi_sta_disconnected.reason);
      gSawDisconnect = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      gGotAddress = true;
      break;
    default:
      break;
  }
}

}  // namespace

bool ArduinoWifiRadio::up() {
  if (onState_) return true;
  // Registered once, and before the interface comes up so no event can be
  // missed between the two.
  static bool handlerInstalled = false;
  if (!handlerInstalled) {
    WiFi.onEvent(onWifiEvent);
    handlerInstalled = true;
  }
  WiFi.persistent(false);  // do not write the credential to the vendor's own NVS
  if (!WiFi.mode(WIFI_STA)) {
    Serial.printf("[wifi] could not bring the interface up in station mode\n");
    Serial.flush();
    return false;
  }
  onState_ = true;
  return true;
}

bool ArduinoWifiRadio::beginScan() {
  if (!up()) return false;
  results_.clear();
  WiFi.scanDelete();
  // async = true, show_hidden = false. Hidden networks are dropped by
  // rankScanResults anyway -- there is no join-hidden flow, so a blank row
  // would be a choice with nothing behind it -- and not asking for them is
  // cheaper than asking and discarding.
  const int started = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
  if (started == WIFI_SCAN_FAILED) {
    scan_ = reader::ScanState::Failed;
    Serial.printf("[wifi] scan would not start\n");
    Serial.flush();
    return false;
  }
  scan_ = reader::ScanState::Running;
  return true;
}

reader::ScanState ArduinoWifiRadio::scanState() const {
  if (scan_ != reader::ScanState::Running) return scan_;
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return reader::ScanState::Running;
  if (n == WIFI_SCAN_FAILED) {
    scan_ = reader::ScanState::Failed;
    return scan_;
  }
  // Harvested HERE rather than in scanResults(), because scanResults() is
  // const-and-cheap by contract and is called per paint; this runs once per
  // scan. WiFi.scanDelete() frees the driver's copy as soon as we have ours,
  // which matters on a part where the radio's own allocation is the reason
  // this flow is on-demand at all.
  results_.clear();
  results_.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    reader::ScanResult r;
    r.ssid = std::string(WiFi.SSID(i).c_str());
    r.rssi = WiFi.RSSI(i);
    r.locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    results_.push_back(std::move(r));
  }
  WiFi.scanDelete();
  scan_ = reader::ScanState::Done;
  Serial.printf("[wifi] scan found %d network(s)\n", n);
  Serial.flush();
  return scan_;
}

bool ArduinoWifiRadio::beginJoin(std::string_view ssid, std::string_view psk) {
  if (!up()) return false;
  gSawDisconnect = false;
  gGotAddress = false;
  gLastDisconnectReason = 0;
  reason_ = 0;
  const std::string s(ssid);
  const std::string p(psk);
  // An empty passphrase is an OPEN network, which WiFi.begin takes as a null
  // rather than an empty string.
  WiFi.begin(s.c_str(), p.empty() ? nullptr : p.c_str());
  joinStartedMs_ = millis();
  join_ = reader::JoinState::Running;
  return true;
}

reader::JoinState ArduinoWifiRadio::joinState() const {
  if (join_ != reader::JoinState::Running) return join_;

  // AN ADDRESS, NOT MERELY ASSOCIATION. WL_CONNECTED can be true before DHCP
  // has finished, and a join that associated and never got a lease is the
  // Incomplete shape -- which is the one whose sentence says the network took
  // the password and never finished. Waiting for the address is what makes
  // that sentence true when it is shown.
  if (gGotAddress && WiFi.status() == WL_CONNECTED) {
    join_ = reader::JoinState::Ok;
    Serial.printf("[wifi] joined, address %s\n", WiFi.localIP().toString().c_str());
    Serial.flush();
    return join_;
  }

  if (gSawDisconnect) {
    join_ = reader::JoinState::Failed;
    reason_ = gLastDisconnectReason;
    Serial.printf("[wifi] join failed, vendor reason %d\n", reason_);
    Serial.flush();
    return join_;
  }

  if (millis() - joinStartedMs_ >= kJoinTimeoutMs) {
    join_ = reader::JoinState::Failed;
    // ASSOCIATED-BUT-NO-ADDRESS gets the sentinel and everything else gets
    // the vendor's last word. WL_CONNECTED here means association succeeded
    // and DHCP did not, which is exactly what kWifiReasonNoAddress is for --
    // it is not a WIFI_REASON_* at all, so it needs a value of its own.
    reason_ = (WiFi.status() == WL_CONNECTED) ? reader::kWifiReasonNoAddress
                                              : gLastDisconnectReason;
    Serial.printf("[wifi] join timed out after %ums, status=%d reason=%d\n", kJoinTimeoutMs,
                  static_cast<int>(WiFi.status()), reason_);
    Serial.flush();
    return join_;
  }
  return join_;
}

void ArduinoWifiRadio::down() {
  if (!onState_) return;
  WiFi.scanDelete();
  // disconnect(wifioff = true, eraseap = true): the rails go down AND the
  // vendor's own stored credential goes with them. We keep the passphrase in
  // our own namespace, and leaving a second copy in the driver's would be a
  // second place to forget it from when the reader forgets a network.
  WiFi.disconnect(/*wifioff=*/true, /*eraseap=*/true);
  WiFi.mode(WIFI_OFF);
  onState_ = false;
  scan_ = reader::ScanState::Idle;
  join_ = reader::JoinState::Idle;
  results_.clear();
  Serial.printf("[wifi] radio down, free heap %u\n", ESP.getFreeHeap());
  Serial.flush();
}
