#pragma once
#include <cstdint>
#include <deque>
#include "harness_state.h"

// A SCRIPTED STATUS QUEUE, which makes the whole safety ladder drivable from one
// array: the low banner, its hysteresis, the critical shutdown, the resume gate and
// the charge latch all read this and nothing else.
//
// -1 IS NOT 0%. A percentage that has never been read and a battery that is flat
// are different claims, and the screen that reports one must not make the other.
class BatteryMonitor {
 public:
  struct Status {
    bool supported = true;
    bool percentageKnown = true;
    bool millivoltsKnown = true;
    bool chargingKnown = true;
    bool externalPowerKnown = true;
    uint16_t percentage = 72;
    uint16_t millivolts = 3900;
    bool charging = false;
    bool externalPower = false;
    int32_t pm1VinMv = -1;
    int32_t pm1VinOutMv = -1;
    int16_t pm1PowerSource = -1;
  };

  BatteryMonitor() = default;
  explicit BatteryMonitor(int8_t adcPin, float dividerMultiplier = 2.0f,
                          int8_t chargeStatusPin = -1) {
    (void)adcPin;
    (void)dividerMultiplier;
    (void)chargeStatusPin;
  }

  Status readStatus() const {
    if (!queue().empty()) {
      const Status s = queue().front();
      if (queue().size() > 1) queue().pop_front();
      harness::record("<battery> read pct=%u charging=%d", s.percentage, s.charging ? 1 : 0);
      return s;
    }
    Status s;
    harness::record("<battery> read pct=%u charging=%d", s.percentage, s.charging ? 1 : 0);
    return s;
  }

  // A scenario pushes the readings it wants; the last one repeats, because a poll
  // that ran out of script should hold rather than invent a new battery.
  static std::deque<Status>& queue() {
    static std::deque<Status> q;
    return q;
  }
};
