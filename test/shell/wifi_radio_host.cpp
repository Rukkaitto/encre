// THE RADIO'S DESKTOP TWIN, at the reader::WifiRadio seam it already satisfies.
//
// ArduinoWifiRadio is declared in shell/src/wifi_radio_arduino.h; this defines the
// same class over scripted state, so no <WiFi.h> enters the build. That matters
// beyond convenience: linking the real stack costs 21,328 bytes of STATIC RAM on
// the device, paid at boot whether or not the radio is ever switched on, and a
// desktop build cannot reproduce that and must not pretend to.
//
// SCRIPTED, NOT SIMULATED. A scan returns what a scenario put there and a join ends
// how it was told to. The radio's real failure shapes -- a wrong passphrase, a
// network that vanished, one that will not answer -- are distinguishable on the
// glass, so each has to be askable; none of them is DISCOVERED here.
#include "wifi_radio_arduino.h"

#include <string>
#include <vector>

#include "harness_state.h"

namespace harness {

std::vector<reader::ScanResult>& scanResults() {
  static std::vector<reader::ScanResult> r;
  return r;
}
reader::JoinState& joinResult() {
  static reader::JoinState s = reader::JoinState::Ok;
  return s;
}
int& joinReasonCode() {
  static int r = 0;
  return r;
}
reader::ScanState& scanResult() {
  static reader::ScanState s = reader::ScanState::Done;
  return s;
}

}  // namespace harness

bool ArduinoWifiRadio::up() {
  if (!onState_) harness::record("<wifi> up");
  onState_ = true;
  return true;
}

bool ArduinoWifiRadio::beginScan() {
  up();
  results_ = harness::scanResults();
  // ONE STEP, WHERE THE DEVICE TAKES SECONDS. The shell polls scanState() from
  // loop(), so a Running state that never settled would hang a scenario; the
  // transitions the screens care about are Idle -> Running -> Done/Failed, and a
  // scenario that wants to see Running asks for it explicitly.
  scan_ = harness::scanResult();
  harness::record("<wifi> scan -> %s (%zu result(s))",
                  scan_ == reader::ScanState::Done     ? "Done"
                  : scan_ == reader::ScanState::Failed ? "Failed"
                                                       : "Running",
                  results_.size());
  return true;
}

reader::ScanState ArduinoWifiRadio::scanState() const { return scan_; }

bool ArduinoWifiRadio::beginJoin(std::string_view ssid, std::string_view psk) {
  up();
  // THE PASSPHRASE IS NOT RECORDED. It reaches the transcript nowhere: a record
  // that carried it would put a reader's secret in a golden file, and the
  // transcripts are committed.
  harness::record("<wifi> join ssid=%.*s psk=%s", static_cast<int>(ssid.size()), ssid.data(),
                  psk.empty() ? "(open)" : "(set)");
  join_ = harness::joinResult();
  reason_ = harness::joinReasonCode();
  return true;
}

reader::JoinState ArduinoWifiRadio::joinState() const { return join_; }

void ArduinoWifiRadio::down() {
  if (onState_) harness::record("<wifi> down");
  onState_ = false;
  scan_ = reader::ScanState::Idle;
  join_ = reader::JoinState::Idle;
  // ~21.5 KB OF FREE HEAP DOES NOT COME BACK AFTER down() ON THE DEVICE, and one
  // TLS handshake fragments the largest free BLOCK for the rest of the session --
  // which is why a sync ends in a restart rather than a down(). Neither is
  // reproduced here. A scenario that wants the consequence scripts the heap.
}
