#include <string>

#include "doctest.h"
#include "golden.h"
#include "reader/framebuffer.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

TEST_CASE("QuietTheme renders Home to golden") {
  auto labelFont = golden::slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_500_16.rfnt");
  auto valueFont = golden::slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_700_16.rfnt");
  reader::QuietTheme theme;
  REQUIRE(theme.loadFonts(labelFont.data(), labelFont.size(), valueFont.data(), valueFont.size()));

  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;  // focus on Continue
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  reader::Framebuffer fb(480, 800);
  theme.renderHome(fb, vm);

  golden::checkGolden(fb, "home_quiet");
}
