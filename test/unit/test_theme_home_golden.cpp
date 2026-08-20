#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("QuietTheme renders Home to golden") {
  auto ui = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_16.rfnt");
  reader::QuietTheme theme;
  REQUIRE(theme.loadFonts(ui.data(), ui.size()));

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

  const std::string golden = std::string(GOLDEN_DIR) + "/home_quiet.png";
  if (!std::ifstream(golden).good()) {
    reader::writePng(fb, (std::string(BUILD_DIR) + "/home_quiet_candidate.png").c_str());
    FAIL("golden missing - inspect build/home_quiet_candidate.png, then copy to test/golden/home_quiet.png");
  }
  CHECK(reader::comparePng(fb, golden.c_str()));
}
