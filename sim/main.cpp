#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f.good()) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
  if (argc < 3 || std::string(argv[1]) != "home") {
    std::fprintf(stderr, "usage: reader_sim home OUT.png\n");
    return 2;
  }
  auto labelFont = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_500_16.rfnt");
  auto valueFont = slurp(std::string(ASSETS_DIR) + "/built/spacegrotesk_700_16.rfnt");
  reader::QuietTheme theme;
  if (labelFont.empty() || valueFont.empty() ||
      !theme.loadFonts(labelFont.data(), labelFont.size(), valueFont.data(), valueFont.size())) {
    std::fprintf(stderr, "failed to load ui fonts (run: make fonts)\n");
    return 1;
  }
  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  reader::Framebuffer fb(480, 800);
  theme.renderHome(fb, vm);
  if (!reader::writePng(fb, argv[2])) return 1;
  std::printf("wrote %s\n", argv[2]);
  return 0;
}
