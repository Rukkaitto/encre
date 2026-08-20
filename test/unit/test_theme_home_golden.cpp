#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static reader::HomeViewModel sampleHome() {
  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}

TEST_CASE("QuietTheme renders Home to golden on both panel geometries") {
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  auto a = slurp(dir + "spacegrotesk_500_12.rfnt");
  auto b = slurp(dir + "spacegrotesk_500_13.rfnt");
  auto c = slurp(dir + "spacegrotesk_700_14.rfnt");
  auto d = slurp(dir + "spacegrotesk_500_17.rfnt");
  auto e = slurp(dir + "spacegrotesk_700_24.rfnt");
  auto g = slurp(dir + "spacegrotesk_700_44.rfnt");
  reader::FontSet fonts;
  fonts.load(reader::Role::Meta, a.data(), a.size());
  fonts.load(reader::Role::Label, b.data(), b.size());
  fonts.load(reader::Role::Value, c.data(), c.size());
  fonts.load(reader::Role::Body, d.data(), d.size());
  fonts.load(reader::Role::Title, e.data(), e.size());
  fonts.load(reader::Role::Display, g.data(), g.size());
  REQUIRE(fonts.ready());

  reader::QuietTheme theme;

  // Home goes through the grayscale path: three passes, and the golden holds
  // the 4-level composition of the two planes. The Bw pass is rendered too, so
  // the test drives the same sequence the shell and the simulator do.
  auto renderThree = [&](int w, int h, const std::string& name) {
    reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
    const reader::HomeViewModel vm = sampleHome();
    theme.renderHome(bw, fonts, vm, reader::Plane::Bw);
    theme.renderHome(lsb, fonts, vm, reader::Plane::Lsb);
    theme.renderHome(msb, fonts, vm, reader::Plane::Msb);
    golden::checkGoldenGray(lsb, msb, name);
  };

  SUBCASE("X4 480x800") { renderThree(480, 800, "home_quiet"); }
  SUBCASE("X3 528x792") { renderThree(528, 792, "home_quiet_x3"); }
}
