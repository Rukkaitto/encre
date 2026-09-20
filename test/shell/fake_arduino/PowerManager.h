#pragma once
#include "harness_state.h"

namespace freeink {

// A SLEEP THROWS RATHER THAN NOT RETURNING, so the scenario can catch it, write
// the transcript, and -- for a wake scenario -- re-enter setup() with the NVS fake
// intact and a different reset reason. A [[noreturn]] function that throws is
// legal: control does not return normally.
struct HarnessSlept {};

class PowerManager {
 public:
  static bool armPowerButtonWakeup() {
    harness::record("<power> armPowerButtonWakeup");
    return true;
  }
  // THE SDK ALREADY HANDLES THE FINGER STILL BEING ON THE BUTTON, which is why
  // sleeping on the DOWN edge cannot be satisfied by the press that asked for it.
  static void waitForPowerButtonRelease() {
    harness::record("<power> waitForPowerButtonRelease");
    harness::powerButtonDown() = false;
  }
  // Cuts the X3's SD rail despite the SDK header calling it a no-op on X3/X4 --
  // the profile declares sd.powerEnable = 13.
  static void powerDownRailsForSleep() { harness::record("<power> railsDown"); }
  [[noreturn]] static void deepSleepUntilPowerButton() {
    harness::record("<power> deepSleep");
    throw HarnessSlept{};
  }
};

}  // namespace freeink
