#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/png.h"
#include "reader/screen_home.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f.good()) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

// The ramp, and the bytes behind it. A `FontSet` owns nothing -- every `Font`
// is a zero-copy view into the asset blob (core/include/reader/fontset.h) -- so
// the blobs must outlive it and cannot be locals of the loader. Extracting this
// as `loadRamp(FontSet&)` with the vectors inside compiled, linked, ran, and
// drew garbage in place of MIDDLEMARCH and 6%: whichever faces the freed heap
// got reused for. Keeping the bytes next to the set is what makes that
// impossible, and it is the same shape `test/unit/ramp.h` uses.
struct SimRamp {
  std::vector<uint8_t> meta400, meta500, label400, label500, value500, value700;
  std::vector<uint8_t> body400, body500, title700, display700;
  reader::FontSet fonts;
};

// One asset per role, and the role names the weight it wants: FontSet::load
// refuses a blob whose declared size or weight is not the role's, so a
// transposed pair here fails at startup instead of drawing a screen in the
// wrong weight.
static bool loadRamp(SimRamp& r) {
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
  r.meta400 = slurp(dir + "spacegrotesk_400_10pt.rfnt");
  r.meta500 = slurp(dir + "spacegrotesk_500_10pt.rfnt");
  r.label400 = slurp(dir + "spacegrotesk_400_11pt.rfnt");
  r.label500 = slurp(dir + "spacegrotesk_500_11pt.rfnt");
  r.value500 = slurp(dir + "spacegrotesk_500_12pt.rfnt");
  r.value700 = slurp(dir + "spacegrotesk_700_12pt.rfnt");
  r.body400 = slurp(dir + "spacegrotesk_400_14pt.rfnt");
  r.body500 = slurp(dir + "spacegrotesk_500_14pt.rfnt");
  r.title700 = slurp(dir + "spacegrotesk_700_20pt.rfnt");
  r.display700 = slurp(dir + "spacegrotesk_700_32pt.rfnt");
  r.fonts.load(reader::Role::Meta400, r.meta400.data(), r.meta400.size());
  r.fonts.load(reader::Role::Meta500, r.meta500.data(), r.meta500.size());
  r.fonts.load(reader::Role::Label400, r.label400.data(), r.label400.size());
  r.fonts.load(reader::Role::Label500, r.label500.data(), r.label500.size());
  r.fonts.load(reader::Role::Value500, r.value500.data(), r.value500.size());
  r.fonts.load(reader::Role::Value700, r.value700.data(), r.value700.size());
  r.fonts.load(reader::Role::Body400, r.body400.data(), r.body400.size());
  r.fonts.load(reader::Role::Body500, r.body500.data(), r.body500.size());
  r.fonts.load(reader::Role::Title700, r.title700.data(), r.title700.size());
  r.fonts.load(reader::Role::Display700, r.display700.data(), r.display700.size());
  if (!r.fonts.ready()) {
    std::fprintf(stderr, "font ramp failed to load from %s\n", dir.c_str());
    return false;
  }
  return true;
}

// "DOWN,CONFIRM,CONFIRM+" -> events. A trailing '+' means a long press, which is
// how a scripted run reaches a hold without a clock.
static bool parseKeys(const char* spec, std::vector<reader::InputEvent>& out) {
  const std::string s(spec);
  size_t i = 0;
  while (i <= s.size()) {
    const size_t comma = s.find(',', i);
    std::string tok = s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
    if (!tok.empty()) {
      reader::PressKind kind = reader::PressKind::Short;
      if (tok.back() == '+') {
        kind = reader::PressKind::Long;
        tok.pop_back();
      }
      reader::Button b;
      if (tok == "BACK") b = reader::Button::Back;
      else if (tok == "CONFIRM") b = reader::Button::Confirm;
      else if (tok == "LEFT") b = reader::Button::Left;
      else if (tok == "RIGHT") b = reader::Button::Right;
      else if (tok == "UP") b = reader::Button::Up;
      else if (tok == "DOWN") b = reader::Button::Down;
      else if (tok == "POWER") b = reader::Button::Power;
      else {
        std::fprintf(stderr, "unknown key '%s'\n", tok.c_str());
        return false;
      }
      out.push_back({b, kind});
    }
    if (comma == std::string::npos) break;
    i = comma + 1;
  }
  return true;
}

// Render `top` the way the shell would and write the PNG argv[2] asks for.
//
// Whichever path the screen on top declares, so a design comparison is measuring
// what the firmware will actually paint. Getting this wrong is worse than
// untidy: `tools/compare-design.py` drives this binary, so a simulator rendering
// a path the shell no longer takes turns every comparison into a check against
// dead code. That is why this is one function both subcommands go through rather
// than a plane argument each of them picks for itself -- the `home` subcommand
// used to hardcode BwDithered, which would now be exactly that dead path.
static bool renderToPng(const reader::Screen& top, const reader::FontSet& fonts,
                        reader::Theme& theme, int w, int h, const char* out) {
  if (top.fidelity() == reader::Fidelity::Grayscale) {
    // The three-pass path: a thresholded base frame plus the two bit-planes the
    // controller combines into 4 levels, recomposed here into one greyscale
    // image so the desktop sees what the panel will paint. `bw` is rendered (not
    // skipped) so the simulator drives the same call sequence the shell does.
    reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
    top.render(bw, fonts, theme, reader::Plane::Bw);
    top.render(lsb, fonts, theme, reader::Plane::Lsb);
    top.render(msb, fonts, theme, reader::Plane::Msb);
    return reader::writeGrayPng(lsb, msb, out);
  }
  // Both one-pass paths write a two-level PNG; they differ only in whether
  // partial coverage is thresholded (Mono, what chrome ships) or stippled
  // (Dithered).
  const reader::Plane plane = top.fidelity() == reader::Fidelity::Dithered
                                  ? reader::Plane::BwDithered
                                  : reader::Plane::Bw;
  reader::Framebuffer fb(w, h);
  top.render(fb, fonts, theme, plane);
  return reader::writePng(fb, out);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: reader_sim home|app OUT.png [--canvas WxH] [--keys SPEC]\n");
    return 2;
  }
  int w = 480, h = 800;
  const char* keys = nullptr;
  for (int i = 3; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--canvas") == 0) std::sscanf(argv[i + 1], "%dx%d", &w, &h);
    else if (std::strcmp(argv[i], "--keys") == 0) keys = argv[i + 1];
  }

  const bool isHome = std::strcmp(argv[1], "home") == 0;
  const bool isApp = std::strcmp(argv[1], "app") == 0;
  if (!isHome && !isApp) {
    std::fprintf(stderr, "unknown screen '%s' (expected 'home' or 'app')\n", argv[1]);
    return 3;
  }

  SimRamp ramp;
  if (!loadRamp(ramp)) return 1;
  const reader::FontSet& fonts = ramp.fonts;

  reader::QuietTheme theme;

  if (isHome) {
    // From the shared catalogue, not from a copy here: the simulator's PNGs are
    // only evidence about the device if the device shows the same content. And
    // through the real HomeScreen rather than calling theme.renderHome directly,
    // so the fidelity this renders is the one the screen declares -- Home takes
    // the default, Fidelity::Mono, one pass and one 1-bit image.
    const reader::HomeScreen home(reader::demoHomeVm(), reader::demoHomeTargets());
    if (!renderToPng(home, fonts, theme, w, h, argv[2])) return 1;
    std::printf("wrote %s (%dx%d)\n", argv[2], w, h);
    return 0;
  }

  std::vector<reader::InputEvent> events;
  if (keys != nullptr && !parseKeys(keys, events)) return 2;

  // Same root, same factory the shell builds, so a scripted desktop run walks
  // the screens the device walks rather than a second, similar-looking set.
  reader::DemoScreenFactory factory;
  reader::App app(
      std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
      factory);
  for (const reader::InputEvent& ev : events) app.dispatch(ev);

  if (!renderToPng(app.top(), fonts, theme, w, h, argv[2])) return 1;
  std::printf("wrote %s (%dx%d) screen=%d depth=%d\n", argv[2], w, h,
              static_cast<int>(app.top().id()), app.depth());
  return 0;
}
