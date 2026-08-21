#include "reader/screens.h"

#include "reader/screen_input_monitor.h"
#include "reader/screen_sd_missing.h"
#include "reader/screen_stub.h"

namespace reader {

HomeViewModel demoHomeVm() {
  HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 \xE2\x80\x94 MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  // Home binds no long press, so no slot shows a ring.
  vm.holds = {false, false, false, false};
  return vm;
}

std::vector<ScreenId> demoHomeTargets() { return {ScreenId::Library, ScreenId::Settings}; }

std::unique_ptr<Screen> DemoScreenFactory::create(ScreenId id) {
  using Row = StubScreen::Row;
  switch (id) {
    case ScreenId::Library:
      return std::make_unique<StubScreen>(
          ScreenId::Library, "LIBRARY",
          std::vector<Row>{{"CLASSICS", std::nullopt}, {"MIDDLEMARCH", std::nullopt}});
    case ScreenId::Settings:
      // The Input Monitor is reachable ONLY from here. Nothing else lists it, and
      // without a way in, the phase loses the one place short-versus-long
      // classification and the FAST refresh path are visible on the panel.
      return std::make_unique<StubScreen>(
          ScreenId::Settings, "SETTINGS",
          std::vector<Row>{{"INPUT MONITOR", ScreenId::InputMonitor}, {"ABOUT", std::nullopt}});
    case ScreenId::InputMonitor:
      return std::make_unique<InputMonitorScreen>();
    case ScreenId::SdMissing:
      // Buildable through the factory, not only as a root, so the shell can
      // replace the stack with it if the card goes away later and the simulator
      // can render it. It takes no arguments: a missing card is a missing card.
      return std::make_unique<SdMissingScreen>();
    case ScreenId::Home:
      // The root is never rebuilt: popping to Home returns the original object,
      // with its focus intact.
      return nullptr;
  }
  return nullptr;
}

}  // namespace reader
