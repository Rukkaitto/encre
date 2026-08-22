#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/booklist.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/host_fs.h"
#include "reader/png.h"
#include "reader/screen_home.h"
#include "reader/screen_library.h"
#include "reader/screen_sd_missing.h"
#include "reader/screens.h"
#include "reader/settings.h"
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
  std::vector<uint8_t> body400, body500, body700, title700, display700;
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
  r.body700 = slurp(dir + "spacegrotesk_700_14pt.rfnt");
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
  r.fonts.load(reader::Role::Body700, r.body700.data(), r.body700.size());
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
// The paint, as one callable per pass, so both what follows can drive the same
// fidelity logic: a lone screen (`home`, `sd_missing`) and a whole App stack
// (everything with an overlay in it, where painting only the top would draw a
// panel floating on white).
using PaintPass = std::function<void(reader::Framebuffer&, reader::Plane)>;

static bool renderPassesToPng(const PaintPass& paint, reader::Fidelity fidelity, int w, int h,
                              const char* out) {
  if (fidelity == reader::Fidelity::Grayscale) {
    // The three-pass path: a thresholded base frame plus the two bit-planes the
    // controller combines into 4 levels, recomposed here into one greyscale
    // image so the desktop sees what the panel will paint. `bw` is rendered (not
    // skipped) so the simulator drives the same call sequence the shell does.
    reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
    paint(bw, reader::Plane::Bw);
    paint(lsb, reader::Plane::Lsb);
    paint(msb, reader::Plane::Msb);
    return reader::writeGrayPng(lsb, msb, out);
  }
  // Both one-pass paths write a two-level PNG; they differ only in whether
  // partial coverage is thresholded (Mono, what chrome ships) or stippled
  // (Dithered).
  const reader::Plane plane = fidelity == reader::Fidelity::Dithered ? reader::Plane::BwDithered
                                                                     : reader::Plane::Bw;
  reader::Framebuffer fb(w, h);
  paint(fb, plane);
  return reader::writePng(fb, out);
}

static bool renderToPng(const reader::Screen& top, const reader::FontSet& fonts,
                        reader::Theme& theme, int w, int h, const char* out) {
  return renderPassesToPng(
      [&](reader::Framebuffer& fb, reader::Plane p) { top.render(fb, fonts, theme, p); },
      top.fidelity(), w, h, out);
}

// Through App::render, which paints the topmost non-overlay screen and then every
// overlay above it. Fidelity still comes from the top of the stack alone.
static bool renderAppToPng(const reader::App& app, const reader::FontSet& fonts,
                           reader::Theme& theme, int w, int h, const char* out) {
  return renderPassesToPng(
      [&](reader::Framebuffer& fb, reader::Plane p) { app.render(fb, fonts, theme, p); },
      app.top().fidelity(), w, h, out);
}

// The presses that reach the Library with the row its board focuses: Confirm on
// Home's LIBRARY row (the first one, which is where Home's focus lands after one
// Down), then one Down inside it. Shared with the overlay subcommands, which
// need the same journey with a different row at the end of it.
static std::vector<reader::InputEvent> libraryEntry(int downsInLibrary = 1) {
  using reader::Button;
  using reader::PressKind;
  std::vector<reader::InputEvent> out{{Button::Down, PressKind::Short},
                                      {Button::Confirm, PressKind::Short}};
  for (int i = 0; i < downsInLibrary; ++i) out.push_back({Button::Down, PressKind::Short});
  return out;
}

// What --root does with the directory it is given: mount a HostFileSystem on it
// and run the shell's own boot-time storage sequence against it, so the storage
// path is exercised on the desktop and not only on the device.
//
// It reports rather than only rendering because the settings half of it still
// changes no pixels; the LIBRARY half does, now that a --root Library lists real
// files (see libraryRootIn below).
static void reportStorage(reader::FileSystem& fs, const std::string& shown) {
  reader::Settings settings;
  const bool ok = reader::loadSettings(fs, settings);
  std::printf("root %s: mounted=%d settings=%s sleepAfterMs=%u fullRefreshEvery=%d "
              "fullOnTransition=%d\n",
              shown.c_str(), (int)fs.mounted(), ok ? "loaded" : "defaulted-or-corrected",
              (unsigned)settings.sleepAfterMs, settings.fullRefreshEvery,
              (int)settings.fullOnTransition);
}

// WHERE THE LIBRARY IS ROOTED INSIDE A --root, and why it is not simply
// kBooksRoot.
//
// The device's Library is rooted at /books, full stop. A --root directory is
// asked to be two things at once: an image of a card, where /books is exactly
// what the device would read, and a plain folder of EPUBs someone dropped
// somewhere to see the screen render real filenames. Insisting on /books would
// make the second one silently list nothing, which is the same
// looks-like-a-bug-but-is-not that setLibraryVisibleRows produces.
//
// So: /books when it is there, the root itself when it is not, and say which.
// A card image behaves exactly like the card; a bare folder of books just works.
static std::string libraryRootIn(reader::FileSystem& fs) {
  const bool hasBooks = fs.exists(reader::kBooksRoot);
  const std::string root = hasBooks ? reader::kBooksRoot : "/";
  // The count Home's LIBRARY row shows on the device, printed here so the
  // desktop exercises BookList::countLibrary against a real directory tree and
  // not only against the fake filesystem in the unit tests.
  std::printf("library root %s (%s), %d book(s) counted\n", root.c_str(),
              hasBooks ? "the device's own, found under --root"
                       : "--root has no /books, so the directory itself is the library",
              reader::BookList::countLibrary(fs, root));
  return root;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: reader_sim home|sd_missing|app OUT.png [--canvas WxH] "
                         "[--keys SPEC] [--root DIR]\n");
    return 2;
  }
  int w = 480, h = 800;
  const char* keys = nullptr;
  const char* root = nullptr;
  // Every option here takes a value, which is what makes `i + 1 < argc` the right
  // bound -- and also what makes a trailing bare flag invisible to this loop. So
  // it is rejected below rather than silently ignored: a `--root` whose directory
  // the caller forgot would otherwise look like it had been honoured.
  for (int i = 3; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--canvas") == 0) std::sscanf(argv[i + 1], "%dx%d", &w, &h);
    else if (std::strcmp(argv[i], "--keys") == 0) keys = argv[i + 1];
    else if (std::strcmp(argv[i], "--root") == 0) root = argv[i + 1];
  }
  if (argc > 3 && std::strncmp(argv[argc - 1], "--", 2) == 0) {
    std::fprintf(stderr, "%s needs a value\n", argv[argc - 1]);
    return 2;
  }
  // The filesystem --root asks for, if any. It has to outlive the App -- the
  // Library holds a reference to it -- so it is declared here rather than inside
  // the reporting call it used to be a local of.
  std::optional<reader::HostFileSystem> hostFs;
  std::string libraryRoot;
  if (root != nullptr) {
    reader::HostFileSystem& fs = hostFs.emplace(root);
    // Before the fonts, because a bad --root is worth reporting even on a run
    // that then fails to find its type ramp.
    reportStorage(fs, fs.root());
    libraryRoot = libraryRootIn(fs);
  }

  // The screen ids here are tools/compare-design.py's: whatever it lists for a
  // board is what it passes as argv[1], so a screen that renders but is not
  // named here is a screen the design comparison reports as "not implemented".
  const bool isHome = std::strcmp(argv[1], "home") == 0;
  const bool isSdMissing = std::strcmp(argv[1], "sd_missing") == 0;
  const bool isApp = std::strcmp(argv[1], "app") == 0;
  // The Library and the three boards that put something over it. Each is the
  // same screen reached by the same presses the device would need, so a
  // comparison sheet is a check on navigation as well as on rendering. The
  // presses are the board's: Library.dc.html focuses its second row, and the
  // overlay boards focus the sixth.
  const bool isLibrary = std::strcmp(argv[1], "library") == 0;
  const bool isLibraryActions = std::strcmp(argv[1], "library_actions") == 0;
  const bool isDeleteConfirm = std::strcmp(argv[1], "delete_confirm") == 0;
  const bool isBookDetails = std::strcmp(argv[1], "book_details") == 0;
  const bool isSettings = std::strcmp(argv[1], "settings") == 0;
  const bool isSleep = std::strcmp(argv[1], "sleep") == 0;
  const bool isHomeEmpty = std::strcmp(argv[1], "home_empty") == 0;
  if (!isHome && !isSdMissing && !isApp && !isLibrary && !isLibraryActions &&
      !isDeleteConfirm && !isBookDetails && !isSettings && !isSleep && !isHomeEmpty) {
    std::fprintf(stderr,
                 "unknown screen '%s' (expected 'home', 'sd_missing', 'library', "
                 "'library_actions', 'delete_confirm', 'book_details', 'settings', "
                 "'sleep', 'home_empty' or 'app')\n",
                 argv[1]);
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

  if (isSdMissing) {
    // Through the real screen, like `home`: the fidelity rendered is the one the
    // screen declares, and the copy on the PNG is the copy the device will show.
    const reader::SdMissingScreen sd;
    if (!renderToPng(sd, fonts, theme, w, h, argv[2])) return 1;
    std::printf("wrote %s (%dx%d)\n", argv[2], w, h);
    return 0;
  }

  std::vector<reader::InputEvent> events;
  if (keys != nullptr && !parseKeys(keys, events)) return 2;

  // Same factory the shell builds, so a scripted desktop run walks the screens
  // the device walks rather than a second, similar-looking set.
  //
  // WITH A --root it is built OVER THAT FILESYSTEM, which is what exercises the
  // storage path on the desktop: the Library scans a real directory, sorts real
  // filenames, counts real folders, and a delete removes a real file. WITHOUT one
  // it keeps the board's sample content, and that is not a fallback -- it is what
  // the eight goldens and every `make compare` sheet pin, so `--root` must stay
  // opt-in or the rendering tests would depend on whatever is in a directory.
  std::optional<reader::DemoScreenFactory> factoryStore;
  if (hostFs.has_value()) factoryStore.emplace(*hostFs, libraryRoot);
  else factoryStore.emplace();
  reader::DemoScreenFactory& factory = *factoryStore;
  // How many Library rows fit on THIS panel. The theme owns the box model and
  // this is the caller that knows the geometry, so it is asked once and the
  // factory carries the answer into every Library it builds. Skipping it would
  // leave the window inert and the list empty -- correctly, since a screen must
  // not draw a row it was not given.
  factory.setLibraryVisibleRows(theme.libraryVisibleRows(h, fonts));
  // The same relationship for Settings, in three numbers rather than one: its
  // items are not all the same height, so the theme reports the box model and the
  // screen counts. No sink -- there is nowhere on a desktop to persist to, and a
  // render must not need one.
  {
    int listH = 0, rowH = 0, headerH = 0;
    theme.settingsMetrics(h, fonts, listH, rowH, headerH);
    factory.setSettingsMetrics(listH, rowH, headerH);
    // The BOARD's values, not the defaults: design/Settings.dc.html states
    // `10 MIN` and `EVERY 15 PAGES`, which are a plausible configured state rather
    // than a fresh device's. Rendering the defaults here would report a mismatch
    // against the board on every run and it would be the board that was right.
    reader::Settings shown;
    shown.sleepAfterMs = 10u * 60u * 1000u;
    shown.fullRefreshEvery = 15;
    shown.fullOnTransition = true;
    factory.setSettings(shown);
  }
  // The empty variant is a different ROOT, not a different navigation: it is Home
  // with nothing to continue, so there is no journey that reaches it -- the card
  // is what decides, and on the desktop that is a choice of view model.
  reader::App app(
      std::make_unique<reader::HomeScreen>(
          isHomeEmpty ? reader::demoHomeEmptyVm() : reader::demoHomeVm(),
          reader::demoHomeTargets()),
      factory);

  if (isLibrary) {
    // Home's first row is LIBRARY, so one Confirm opens it; then the board's own
    // focus, which is its second row. Reached by pressing rather than by
    // assignment, so the render pins the navigation too.
    for (const reader::InputEvent& ev : libraryEntry()) app.dispatch(ev);
  }
  if (isLibraryActions || isDeleteConfirm || isBookDetails) {
    // LibraryActions' own parent copy focuses the SIXTH row, Dubliners -- not the
    // second, which is what Library.dc.html focuses. The two boards disagree, so
    // the journey does too.
    for (const reader::InputEvent& ev : libraryEntry(5)) app.dispatch(ev);
    // ...and the hold that opens the panel, which is the binding the Library's
    // hint ring advertises.
    app.dispatch({reader::Button::Confirm, reader::PressKind::Long});
  }
  if (isBookDetails) {
    // Down to the panel's second row, `Book details`, and Confirm. It is a whole
    // screen rather than an overlay, so what App::render paints is this alone --
    // the Library and the actions panel beneath it are not drawn at all.
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
  }
  if (isDeleteConfirm) {
    // Down to the panel's fourth row, `Delete...`, and Confirm. The board shows
    // the Library behind this rather than the actions panel, and it is right to:
    // the confirm panel is wider and taller than the actions panel and both are
    // centred, so it covers it completely.
    for (int i = 0; i < 3; ++i) app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
  }
  if (isSettings) {
    // TWO Downs, then Confirm. Home's focus starts BEFORE its menu -- on the
    // CONTINUE block -- so the first Down reaches LIBRARY and the second reaches
    // SETTINGS; libraryEntry() above needs only one for the same reason. Reached
    // by pressing rather than by assignment, so the render pins the navigation
    // too, and getting it wrong showed up immediately as a Library in the
    // comparison sheet rather than as a subtly wrong Settings.
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
  }
  if (isSleep) {
    // NOT reached by pressing: nothing navigates to the sleep screen, the idle
    // timer or the power button puts the device there. So it is pushed directly,
    // which is the honest model -- and it is why this screen has no journey to
    // pin the way Library's and Settings' renders do.
    if (!app.pushScreen(reader::ScreenId::Sleep)) {
      std::fprintf(stderr, "the factory refused ScreenId::Sleep\n");
      return 1;
    }
  }
  for (const reader::InputEvent& ev : events) app.dispatch(ev);

  // Through App::render, not top().render: with an overlay on the stack the top
  // screen alone is a panel floating on white, and one paint path is what keeps
  // the simulator, the goldens and the shell from disagreeing about that.
  if (!renderAppToPng(app, fonts, theme, w, h, argv[2])) return 1;
  std::printf("wrote %s (%dx%d) screen=%d depth=%d\n", argv[2], w, h,
              static_cast<int>(app.top().id()), app.depth());
  return 0;
}
