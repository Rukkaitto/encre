#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// The demo content Phase 2B navigates. Real content arrives in Phase 2C from the
// SD card and the settings store; until then this is the single definition both
// the simulator and the shell build from, so a screenshot from the desktop is
// evidence about the device rather than about a second, similar-looking
// catalogue.
HomeViewModel demoHomeVm();

// Home's menu rows, in order, and the screen each one opens.
std::vector<ScreenId> demoHomeTargets();

class DemoScreenFactory : public ScreenFactory {
 public:
  std::unique_ptr<Screen> create(ScreenId id) override;
};

}  // namespace reader
