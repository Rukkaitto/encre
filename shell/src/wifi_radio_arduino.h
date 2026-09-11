#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/wifi_radio.h"

// THE REAL RADIO, over Arduino `WiFi`, and POLL-SHAPED because the interface
// is -- see reader/wifi_radio.h, which states the reason: Arduino's Wi-Fi
// events arrive on the system event task and `shell/src/main.cpp` creates NO
// TASKS AT ALL. A callback mutating App state from another task would be the
// riskiest thing in this feature.
//
// So nothing here is asynchronous on our side. `WiFi.scanNetworks(true)`
// returns immediately and `WiFi.scanComplete()` is polled; `WiFi.begin()`
// returns immediately and `WiFi.status()` is polled. Both are driven from
// loop()'s quiet window, which is where every other slow job in this firmware
// already lives.
//
// THE RADIO IS DOWN EXCEPT WHILE IT IS BEING USED, and on this hardware that
// is forced rather than chosen: the static allocation alone is ~23 KB against
// a measured 13,696-byte floor with a book open. `down()` is called after
// READY and after every failure, and `up()` is implicit in beginScan/beginJoin
// so no caller can forget it.
class ArduinoWifiRadio : public reader::WifiRadio {
 public:
  bool beginScan() override;
  reader::ScanState scanState() const override;
  const std::vector<reader::ScanResult>& scanResults() const override { return results_; }

  bool beginJoin(std::string_view ssid, std::string_view psk) override;
  reader::JoinState joinState() const override;
  int joinReason() const override { return reason_; }

  void down() override;

 private:
  // Brings the interface up in station mode. Idempotent.
  bool up();

  mutable std::vector<reader::ScanResult> results_;
  mutable reader::ScanState scan_ = reader::ScanState::Idle;
  mutable reader::JoinState join_ = reader::JoinState::Idle;
  mutable int reason_ = 0;
  // WHEN THE JOIN STARTED, because Arduino's WiFi.status() has no "gave up"
  // state for the case that matters most: an AP that is simply not there
  // leaves it at WL_IDLE_STATUS or WL_DISCONNECTED indefinitely. Without a
  // deadline the CONNECTING... dialog would sit on the glass for ever.
  mutable uint32_t joinStartedMs_ = 0;
  bool onState_ = false;
};
