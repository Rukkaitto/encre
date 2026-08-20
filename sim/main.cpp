#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "reader/fontset.h"
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
  if (argc < 3) {
    std::fprintf(stderr, "usage: reader_sim SCREEN OUT.png [--canvas WxH]\n");
    return 2;
  }
  int w = 480, h = 800;
  for (int i = 3; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--canvas") == 0) std::sscanf(argv[i + 1], "%dx%d", &w, &h);

  if (std::strcmp(argv[1], "home") != 0) {
    std::fprintf(stderr, "unknown screen '%s' (only 'home' so far)\n", argv[1]);
    return 3;
  }

  // One asset per role, and the role names the weight it wants: FontSet::load
  // refuses a blob whose declared size or weight is not the role's, so a
  // transposed pair here fails at startup instead of drawing a screen in the
  // wrong weight.
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  auto meta400 = slurp(dir + "spacegrotesk_400_10pt.rfnt");
  auto label500 = slurp(dir + "spacegrotesk_500_11pt.rfnt");
  auto value700 = slurp(dir + "spacegrotesk_700_12pt.rfnt");
  auto body400 = slurp(dir + "spacegrotesk_400_14pt.rfnt");
  auto body500 = slurp(dir + "spacegrotesk_500_14pt.rfnt");
  auto title700 = slurp(dir + "spacegrotesk_700_20pt.rfnt");
  auto display700 = slurp(dir + "spacegrotesk_700_32pt.rfnt");
  reader::FontSet fonts;
  fonts.load(reader::Role::Meta400, meta400.data(), meta400.size());
  fonts.load(reader::Role::Label500, label500.data(), label500.size());
  fonts.load(reader::Role::Value700, value700.data(), value700.size());
  fonts.load(reader::Role::Body400, body400.data(), body400.size());
  fonts.load(reader::Role::Body500, body500.data(), body500.size());
  fonts.load(reader::Role::Title700, title700.data(), title700.size());
  fonts.load(reader::Role::Display700, display700.data(), display700.size());
  if (!fonts.ready()) {
    std::fprintf(stderr, "font ramp failed to load from %s\n", dir.c_str());
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
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  // Three passes, exactly as the firmware drives the panel: a thresholded base
  // frame plus the two bit-planes the controller combines into 4 levels. The
  // simulator recomposes the planes into one greyscale image so the desktop
  // sees what the panel will paint. `bw` is rendered (not skipped) so the
  // simulator exercises the same call sequence the shell does.
  reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
  reader::QuietTheme theme;
  theme.renderHome(bw, fonts, vm, reader::Plane::Bw);
  theme.renderHome(lsb, fonts, vm, reader::Plane::Lsb);
  theme.renderHome(msb, fonts, vm, reader::Plane::Msb);
  if (!reader::writeGrayPng(lsb, msb, argv[2])) return 1;
  std::printf("wrote %s (%dx%d)\n", argv[2], w, h);
  return 0;
}
