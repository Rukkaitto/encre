#pragma once

#include <cstdint>

namespace reader {

// ONE READING OF THE BATTERY, as BatteryMonitor::readStatus() reports it.
//
// The per-field `Known` flags are the load-bearing part and the reason this is not
// two ints. An I2C gauge read can fail transiently, and BatteryMonitor's unchecked
// accessors answer a FAILURE with 0 -- readPercentage() returns 0, and
// percentageFromMillivolts maps a failed 0 mV to 0% rather than 100%, deliberately.
// So "0%" and "I could not read it" are the same value out of that API and must not
// be the same value in here: a tracker that conflated them would put a flat battery
// on the panel of a device that is fine.
struct BatteryReading {
  bool percentKnown = false;
  int percent = 0;  // 0..100 when percentKnown
  bool chargingKnown = false;
  bool charging = false;
};

// WHAT THE BAND SHOULD SAY, AND WHETHER PLUGGING IN IS WORTH A REPAINT.
//
// In core/ rather than shell/ for the reason ProgressSaveGate is: this is a latch
// with a dwell timer and a session cap, `shell/` has no test harness, and five bugs
// have hidden there. It keeps no clock of its own -- `nowMs` is passed in, because
// core/ has no clock and must not acquire one.
class BatteryTracker {
 public:
  // -1 = never read. Chosen to match homeVmForCard()'s LIBRARY count, which is -1
  // for "the directory could not be read" and draws a blank row rather than a 0:
  // "no books" and "could not look" are different claims, and so are "flat" and
  // "did not answer".
  static constexpr int kUnknownPercent = -1;

  void update(const BatteryReading& r, uint32_t nowMs) {
    if (r.percentKnown) {
      percent_ = r.percent;
      havePercent_ = true;
    }
    if (r.chargingKnown) {
      charging_ = r.charging;
      haveCharging_ = true;
    }
    (void)nowMs;  // the latch, Task 2
  }

  int percent() const { return havePercent_ ? percent_ : kUnknownPercent; }
  // false means "not charging" OR "never told", and unlike percent() there is no
  // third answer to give the caller: the only thing ever drawn is the mark's
  // presence or absence, so a device whose board has no charge-status line at
  // all (chargingKnown never true) must render identically to one that read a
  // known "not charging". There is nothing for a sentinel to buy here.
  bool charging() const { return haveCharging_ && charging_; }

 private:
  int percent_ = 0;
  bool havePercent_ = false;
  bool charging_ = false;
  bool haveCharging_ = false;
};

}  // namespace reader
