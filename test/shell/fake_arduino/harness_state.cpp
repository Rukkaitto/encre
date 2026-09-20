#include "harness_state.h"

#include "BoardConfig.h"
#include "Preferences.h"
#include "SDCardManager.h"
#include "XteinkDetect.h"

namespace freeink {

const XteinkDisplayProbeDiag& getXteinkDisplayProbeDiag() { return harness::probeDiag(); }

bool applyXteinkDisplayController() {
  harness::probeDiag().valid = true;
  harness::probeDiag().promoted = harness::promoteToUc8279();
  if (harness::promoteToUc8279())
    BoardConfig::ACTIVE.displayController = BoardConfig::DisplayController::UC8279;
  harness::record("<board> displayProbe promoted=%d", harness::promoteToUc8279() ? 1 : 0);
  return harness::promoteToUc8279();
}

bool selectXteinkDevice() {
  return BoardConfig::selectDevice(harness::verdict() == XteinkVerdict::X3Confirmed
                                       ? BoardConfig::Board::XteinkX3
                                       : BoardConfig::Board::XteinkX4);
}

}  // namespace freeink

namespace harness {

// FOR THE FAKES' OWN TESTS, NOT FOR SCENARIOS. One scenario per process is the
// rule: every piece of state in main.cpp is a file-static with a boot-time
// initialiser and there is no reset function for it, so a second scenario in one
// process would run against the first one's App, settings and factory. Writing that
// reset would be a change to untested code before the net exists, which inverts the
// whole sequence.
void resetAll() {
  transcript().clear();
  clock_() = Clock{};
  heap() = Heap{};
  powerButtonDown() = false;
  resetReason() = 1;  // ESP_RST_POWERON
  wakeCause() = 0;    // ESP_SLEEP_WAKEUP_UNDEFINED
  usbHostPresent() = false;
  usbPlugged() = false;
  nvs().clear();
  cardPresent() = true;
  cardRoot().clear();
  verdict() = freeink::XteinkVerdict::X3Confirmed;
  promoteToUc8279() = true;
  probeDiag() = freeink::XteinkDisplayProbeDiag{};
  BoardConfig::ACTIVE = BoardConfig::Profile{};
}

}  // namespace harness
