#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/book.h"
#include "reader/booklist.h"
#include "reader/cover.h"
#include "reader/cover_fit.h"
#include "reader/font_manifest.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/host_fs.h"
#include "reader/png.h"
#include "reader/pngd.h"
#include "reader/profile.h"
#include "reader/screen_home.h"
#include "reader/screen_library.h"
#include "reader/screen_sd_missing.h"
#include "reader/scalablefont.h"
#include "reader/screen_book_end.h"
#include "reader/screen_contents.h"
#include "reader/screen_reader.h"
#include "reader/screen_peek.h"
#include "reader/screen_reader_menu.h"
#include "reader/screen_sleep.h"
#include "reader/screen_typography.h"
#include "reader/screens.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"
#include "reader/xml.h"  // BufferSource -- the ByteSource over bytes already in RAM

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
//
// WHICH asset backs WHICH role is the manifest's (reader/font_manifest.h): one
// list, expanded here over file loads and in the shell over embedded arrays.
struct SimRamp {
#define ENCRE_ROLE_BLOB(role, stem) std::vector<uint8_t> blob_##role;
  READER_FONT_RAMP(ENCRE_ROLE_BLOB)
#undef ENCRE_ROLE_BLOB
  reader::FontSet fonts;
};

// One asset per role, and the role names the weight it wants: FontSet::load
// refuses a blob whose declared size or weight is not the role's, so a wrong
// stem in the manifest fails at startup instead of drawing a screen in the
// wrong weight.
static bool loadRamp(SimRamp& r) {
  const std::string dir = std::string(ASSETS_DIR) + "/built/";
#define ENCRE_LOAD_ROLE(role, stem)                                       \
  r.blob_##role = slurp(dir + #stem ".rfnt");                             \
  r.fonts.load(reader::Role::role, r.blob_##role.data(), r.blob_##role.size());
  READER_FONT_RAMP(ENCRE_LOAD_ROLE)
#undef ENCRE_LOAD_ROLE
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

// --bench N: repeat the paint N times and report per-pass microseconds instead of
// only writing the PNG. The desktop cannot tell you what a waveform costs, but the
// RENDER is the same code the shell runs, so this is the one half of a paint the
// desktop can measure honestly -- and it is the half that sits between the button
// and the waveform starting.
static int gBenchIters = 0;

// The desktop's clock for reader::Profile. The device installs micros(); this is
// the same job with std::chrono, so `--bench` reports the SAME breakdown the
// device's `[render]` line does and a change can be judged before it is flashed.
static uint32_t simMicros() {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

static void benchPaint(const PaintPass& paint, reader::Plane plane, int w, int h,
                       const char* label) {
  if (gBenchIters <= 0) return;
  reader::Framebuffer fb(w, h);
  // One warm pass first: the glyph cache is cold on the very first render and the
  // shell's cache is warm for every paint but the first after a font change.
  fb.clear(true);
  paint(fb, plane);
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < gBenchIters; ++i) {
    fb.clear(true);
    paint(fb, plane);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double us =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000.0 / gBenchIters;
  std::printf("[bench] %s %dx%d %.1f us/pass (n=%d)\n", label, w, h, us, gBenchIters);

  // ...and where inside the pass it went. Timed over ONE further pass with the
  // profiler installed, not over the loop above, so the clock reads the span
  // costs add are not folded into the headline figure.
  reader::Profile::install(simMicros);
  reader::Profile::reset();
  fb.clear(true);
  paint(fb, plane);
  double accounted = 0;
  for (int i = 0; i < reader::kPhaseCount; ++i) {
    const auto ph = static_cast<reader::Phase>(i);
    const uint32_t m = reader::Profile::micros(ph);
    if (m == 0) continue;
    accounted += m;
    std::printf("[bench]   %-7s %6u us  x%-4u\n", reader::Profile::name(ph), m,
                reader::Profile::calls(ph));
  }
  std::printf("[bench]   %-7s %6.0f us  (layout, measuring, wrapping)\n", "other",
              us - accounted > 0 ? us - accounted : 0);
  reader::Profile::install(nullptr);
}

static bool renderPassesToPng(const PaintPass& paint, reader::Fidelity fidelity, int w, int h,
                              const char* out) {
  if (fidelity == reader::Fidelity::Grayscale) {
    // The three-pass path: a thresholded base frame plus the two bit-planes the
    // controller combines into 4 levels, recomposed here into one greyscale
    // image so the desktop sees what the panel will paint. `bw` is rendered (not
    // skipped) so the simulator drives the same call sequence the shell does.
    reader::Framebuffer bw(w, h), lsb(w, h), msb(w, h);
    benchPaint(paint, reader::Plane::BwDithered, w, h, "gray-fastpass(dithered)");
    benchPaint(paint, reader::Plane::Bw, w, h, "gray-bw");
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
  benchPaint(paint, plane,  w, h,
             plane == reader::Plane::BwDithered ? "dithered" : "mono");
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

// WHERE A DESKTOP COVER DECODE LANDS: two whole planes in RAM.
//
// The FIRMWARE may not do this and that is the entire point of CoverPlaneSink --
// two rows at a time go to the card and the panel is never resident. A desktop
// tool has 64 bits of address space and wants a PNG at the end, so it holds both
// planes exactly as test_cover.cpp's sink does, and for the same reason: what is
// being checked here is decodeCover, not the sink.
//
// PACKED AS Framebuffer PACKS -- imagefit.cpp says so at the pack site (bit
// `0x80 >> (x & 7)`, paper is a SET bit) and cover.cpp derives `planeRowBytes` as
// `(panelW + 7) / 8`, which is Framebuffer::physRowBytes() under Rotation::None.
// So these bytes ARE a framebuffer store and can be viewed as one rather than
// copied pixel by pixel. `begin` checks that rather than trusting it: if the two
// derivations ever part company this refuses instead of writing a sheared PNG.
struct SimCoverSink : reader::CoverPlaneSink {
  int rowBytes = 0, rows = 0;
  std::vector<uint8_t> msb, lsb;

  bool begin(int panelW, int panelH, int planeRowBytes, int declaredRows) override {
    rowBytes = planeRowBytes;
    if (planeRowBytes != (panelW + 7) / 8 || declaredRows != panelH) {
      std::fprintf(stderr,
                   "cover: plane geometry %d bytes x %d rows is not a %dx%d framebuffer store\n",
                   planeRowBytes, declaredRows, panelW, panelH);
      return false;
    }
    msb.reserve(static_cast<size_t>(planeRowBytes) * static_cast<size_t>(declaredRows));
    lsb.reserve(msb.capacity());
    return true;
  }
  bool row(const uint8_t* m, const uint8_t* l) override {
    msb.insert(msb.end(), m, m + rowBytes);
    lsb.insert(lsb.end(), l, l + rowBytes);
    ++rows;
    return true;
  }
  // Nothing to clean up -- the vectors are dropped with the sink, and the PNG is
  // written by the caller only on an Ok result, so there is no half-written file
  // for this to have to remove.
  bool finish(bool) override { return true; }
};

// `cover BOOK.epub OUT.png [--canvas WxH] [--fit fill|whole]`.
//
// IT IS NOT A SCREEN, and that is why it is dispatched here rather than joining
// the table below. Those ids are `tools/compare-design.py`'s: whatever it lists
// for a board is what it passes as argv[1], so a name in that table is a claim
// that a `.dc.html` exists to diff the render against. A cover is a picture out
// of somebody's book and there is no board it could ever match. Keeping it out
// also lets it take an option no screen has (`--fit`) and keeps it out of the
// unknown-screen message, which is that tool's contract and not a usage string.
//
// IT WRITES A PNG rather than the plane pair the device stores, for two reasons:
// a PNG is the one form both a human and tools/covers.py can look at, and Task
// 12's board asset is generated by this command.
//
// THE TIMES IT PRINTS ARE THE DESKTOP'S AND NOTHING ELSE. This project has twice
// paid for treating a desktop figure as a device one -- kEagerCountBytes was set
// 8x wrong that way, because the ratio quoted at it was a RENDER ratio and the
// path being sized was SD reads plus an inflate on a part with no FPU. A cover
// decode is that second kind of work throughout. The device's own measurement is
// a separate job.
static int runCover(int argc, char** argv) {
  int w = 480, h = 800;
  reader::CoverFit fit = reader::CoverFit::Fill;
  for (int i = 3; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--canvas") == 0) {
      // THE RETURN IS CHECKED, which the screen path below does not do: an
      // unparseable canvas there leaves the default silently, and a cover that
      // rendered at 480x800 after being asked for something else would be a wrong
      // picture reported as a good one.
      if (std::sscanf(argv[i + 1], "%dx%d", &w, &h) != 2) {
        std::fprintf(stderr, "cover: --canvas wants WxH, not '%s'\n", argv[i + 1]);
        return 2;
      }
    } else if (std::strcmp(argv[i], "--fit") == 0) {
      if (std::strcmp(argv[i + 1], "fill") == 0) fit = reader::CoverFit::Fill;
      else if (std::strcmp(argv[i + 1], "whole") == 0) fit = reader::CoverFit::Whole;
      else {
        std::fprintf(stderr, "cover: --fit takes 'fill' or 'whole', not '%s'\n", argv[i + 1]);
        return 2;
      }
    }
  }
  if (argc > 3 && std::strncmp(argv[argc - 1], "--", 2) == 0) {
    std::fprintf(stderr, "%s needs a value\n", argv[argc - 1]);
    return 2;
  }
  // REFUSED HERE RATHER THAN DOWNSTREAM. A non-positive canvas makes a
  // Framebuffer inert and a plane row zero bytes, so the failure would surface as
  // an empty PNG or as the row-count message below -- both of which describe
  // something other than the argument that was wrong.
  if (w <= 0 || h <= 0) {
    std::fprintf(stderr, "cover: --canvas WxH needs two positive numbers, not %dx%d\n", w, h);
    return 2;
  }

  // A HostFileSystem is rooted at a directory and resolves every reader path
  // under it, so a book anywhere on the host is reached by rooting at its folder
  // and opening its bare name. There is no other way in: `fs.readAll("/Users/...")`
  // would look for that path INSIDE the root.
  const std::string book(argv[2]);
  const size_t slash = book.find_last_of('/');
  reader::HostFileSystem fs(slash == std::string::npos ? std::string(".")
                                                       : book.substr(0, slash));
  const std::string inner =
      "/" + (slash == std::string::npos ? book : book.substr(slash + 1));

  const auto t0 = std::chrono::steady_clock::now();
  reader::OpenedBook opened;
  const char* reason = nullptr;
  if (!reader::openBook(fs, inner, opened, &reason)) {
    // NOT a CoverResult: the book did not open at all, so nothing was ever asked
    // about its cover. Reporting this as NoCover would put a book this firmware
    // cannot READ into a column about pictures.
    std::printf("cover result=BookRefused src=0x0 scale=1/1 panel=%dx%d fit=%s "
                "open_ms=%.1f decode_ms=0.0 file=%s reason=%s\n",
                w, h, fit == reader::CoverFit::Fill ? "fill" : "whole",
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count(),
                book.c_str(), reason != nullptr ? reason : "unknown");
    return 1;
  }
  const auto t1 = std::chrono::steady_clock::now();

  SimCoverSink sink;
  reader::CoverReport report;
  const reader::CoverResult r =
      reader::decodeCover(fs, opened, w, h, fit, sink, nullptr, nullptr, &report);
  const auto t2 = std::chrono::steady_clock::now();
  const double openMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double decodeMs = std::chrono::duration<double, std::milli>(t2 - t1).count();

  // FIELD ORDER IS PART OF THE FORMAT. `file=` and `reason=` are the two values
  // that can hold a space, so they go last and in that order -- tools/covers.py
  // takes `reason=` as the tail of the line and `file=` as the tail of what is
  // left. A reason parsed as key=value pairs comes out as its first word, which
  // is how "the OPF is malformed" first appeared in a table as "the".
  std::printf("cover result=%s src=%dx%d scale=1/%d dst=%dx%d+%d+%d panel=%dx%d fit=%s "
              "open_ms=%.1f decode_ms=%.1f file=%s%s%s\n",
              reader::coverResultName(r), report.sourceWidth, report.sourceHeight,
              report.scaleDivisor, report.dstW, report.dstH, report.dstX, report.dstY, w, h,
              fit == reader::CoverFit::Fill ? "fill" : "whole", openMs, decodeMs, book.c_str(),
              report.reason != nullptr ? " reason=" : "",
              report.reason != nullptr ? report.reason : "");
  if (r != reader::CoverResult::Ok) return 1;

  // The planes viewed as framebuffers, not copied into them: see SimCoverSink.
  reader::Framebuffer msbFb(sink.msb.data(), sink.msb.size(), w, h);
  reader::Framebuffer lsbFb(sink.lsb.data(), sink.lsb.size(), w, h);
  // UNREACHABLE while decodeCover keeps its promise -- an Ok result means the sink
  // had exactly panelH rows, so the vectors are exactly a panel. The viewing
  // constructor refuses a store too small rather than clamping, so this is where a
  // broken promise would arrive, and silence here would be a PNG of the wrong shape.
  if (msbFb.sizeBytes() == 0 || lsbFb.sizeBytes() == 0) {
    std::fprintf(stderr, "cover: %d of %d plane rows arrived, which is not a whole panel\n",
                 sink.rows, h);
    return 1;
  }
  if (!reader::writeGrayPng(lsbFb, msbFb, argv[3])) {
    std::fprintf(stderr, "cover: could not write %s\n", argv[3]);
    return 1;
  }
  return 0;
}

// --- The board's own cover, as a CoverSource -------------------------------------
//
// design/SleepCover.dc.html and design/SleepCoverDetails.dc.html put
// `design/assets/sleep-cover-<W>x<H>.png` behind the sleep screen, and this reads
// THAT FILE so the firmware column of the comparison sheet is the same picture the
// design column is. Anything else measures two rasterisers against each other:
// `make compare` renders the board in Chrome and CHROME CANNOT FLOYD-STEINBERG,
// which is the whole reason the asset is generated and committed rather than
// authored (design/assets/README.md).
//
// SO IT DECODES AND DOES NOT RE-FIT. The asset is already this simulator's own
// `cover` output at panel size -- running the source JPEG through CoverFitter again
// here would dither a second time and could only differ from the file the board
// shows. It is also why this reaches for the committed PNG rather than a corpus
// book: the corpus is not in the repo and a machine with no cache would render a
// blank firmware panel against a full-bleed board.
//
// IT IS NOT THE GOLDEN'S SOURCE, AND THE TWO SHOULD NOT BE UNIFIED.
// test_theme_sleep_cover_golden.cpp SYNTHESISES its planes from arithmetic, so that
// a golden does not depend on a PNG decoder -- the decoder is itself under test in
// this feature. A comparison sheet has the opposite requirement: it must hold the
// board's exact bytes. Two purposes, two sources, both right.
struct BoardCover : reader::CoverSource {
  int w = 0, h = 0, rowBytes = 0;
  // Framebuffer-packed plane rows, top row first: bit `0x80 >> (x & 7)`, and paper
  // is a SET bit. Same store shape SimCoverSink above builds.
  std::vector<uint8_t> msb, lsb;

  // Bw inks where coverage >= 2, which is exactly "MSB set" -- so the base pass and
  // the Msb pass read the SAME plane. Honouring that here is what makes this the
  // same shape as the shell's real source; see reader/screen_sleep.h.
  bool loadPlane(reader::Plane plane, reader::Framebuffer& fb) override {
    if (fb.width() != w || fb.height() != h) return false;
    const std::vector<uint8_t>& src = (plane == reader::Plane::Lsb) ? lsb : msb;
    if (src.size() != static_cast<size_t>(rowBytes) * static_cast<size_t>(h)) return false;
    // NOT a memcpy into fb.data(): the planes are LOGICAL raster rows and the shell
    // binds Rotation::Ccw, under which a logical row is a physical COLUMN.
    // writePackedRow is the one function that knows that, and going through it here
    // means the simulator drives the path the device drives.
    for (int y = 0; y < h; ++y)
      fb.writePackedRow(y, src.data() + static_cast<size_t>(y) * static_cast<size_t>(rowBytes));
    return true;
  }
};

// Unpacks the four-level PNG back into the two bit-planes that composed it.
//
// The file is written by writeGrayPng, which maps `(msb << 1) | lsb` through
// `kRamp = {0xFF, 0xAA, 0x55, 0x00}` -- so this inverts exactly that. A byte that
// is NOT one of those four is REFUSED rather than rounded to the nearest: the only
// way one can appear is that the asset stopped being this pipeline's own output,
// which is the fact worth reporting and the one a nearest-level match would hide.
struct BoardCoverSink : reader::ImageRowSink {
  BoardCover* out = nullptr;
  int y = 0;
  const char* refusal = nullptr;

  bool begin(int width, int height) override {
    if (width != out->w || height != out->h) {
      refusal = "the asset is not this panel's size";
      return false;
    }
    out->rowBytes = (out->w + 7) / 8;
    const size_t plane = static_cast<size_t>(out->rowBytes) * static_cast<size_t>(out->h);
    out->msb.assign(plane, 0xFFu);  // all paper; a set level bit CLEARS its bit
    out->lsb.assign(plane, 0xFFu);
    return true;
  }

  bool row(const uint8_t* px) override {
    if (y >= out->h) {
      refusal = "more rows than the header declared";
      return false;
    }
    const size_t at = static_cast<size_t>(y) * static_cast<size_t>(out->rowBytes);
    for (int x = 0; x < out->w; ++x) {
      int level = -1;
      for (int l = 0; l < 4; ++l)
        if (px[x] == kGrayRamp[l]) level = l;
      if (level < 0) {
        refusal = "a grey the four-level ramp does not contain";
        return false;
      }
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
      const size_t byte = at + static_cast<size_t>(x >> 3);
      if ((level & 2) != 0) out->msb[byte] = static_cast<uint8_t>(out->msb[byte] & ~bit);
      if ((level & 1) != 0) out->lsb[byte] = static_cast<uint8_t>(out->lsb[byte] & ~bit);
    }
    ++y;
    return true;
  }

  // png.cpp's own table, and the only place the two files have to agree. It is
  // named rather than inlined so the inversion above reads as the inverse of
  // composeGray rather than as four magic numbers.
  static constexpr uint8_t kGrayRamp[4] = {0xFFu, 0xAAu, 0x55u, 0x00u};
};

// Reads design/assets/sleep-cover-<w>x<h>.png through a HostFileSystem rooted at
// design/, and decodes it with the firmware's own PngDecoder -- the same decoder the
// device would use, rather than the desktop's stb, so the simulator has no second
// image path of its own.
static bool loadBoardCover(BoardCover& out, int w, int h) {
  out.w = w;
  out.h = h;
  char name[64];
  std::snprintf(name, sizeof(name), "/assets/sleep-cover-%dx%d.png", w, h);

  reader::HostFileSystem fs(DESIGN_DIR);
  std::string bytes;
  if (!fs.readAll(name, bytes)) {
    // NAMED, because there is exactly one way to fix it and it is not obvious from
    // a blank panel: the pair is committed, so a missing file means this geometry
    // has no asset rather than that the machine is missing a corpus.
    std::fprintf(stderr, "sleep cover: no %s under %s\n", name, DESIGN_DIR);
    return false;
  }

  reader::BufferSource src(bytes);
  BoardCoverSink sink;
  sink.out = &out;
  reader::PngDecoder dec;
  if (!dec.decode(src, sink)) {
    std::fprintf(stderr, "sleep cover: %s did not decode: %s\n", name,
                 sink.refusal != nullptr ? sink.refusal
                                         : (dec.reason() != nullptr ? dec.reason() : "unknown"));
    return false;
  }
  if (sink.y != h) {
    std::fprintf(stderr, "sleep cover: %s gave %d of %d rows\n", name, sink.y, h);
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: reader_sim home|sd_missing|app OUT.png [--canvas WxH] "
                         "[--keys SPEC] [--root DIR]\n"
                         "       reader_sim cover BOOK.epub OUT.png [--canvas WxH] "
                         "[--fit fill|whole]\n");
    return 2;
  }
  // BEFORE the screen table, and before the type ramp: a cover is not a screen
  // and needs no font. See runCover for why it is kept out of that table.
  if (std::strcmp(argv[1], "cover") == 0) {
    if (argc < 4) {
      std::fprintf(stderr, "usage: reader_sim cover BOOK.epub OUT.png [--canvas WxH] "
                           "[--fit fill|whole]\n");
      return 2;
    }
    return runCover(argc, argv);
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
    else if (std::strcmp(argv[i], "--bench") == 0) gBenchIters = std::atoi(argv[i + 1]);
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
  // design/BookError.dc.html, BookErrorUnreadable.dc.html and BookErrorMemory.dc.html:
  // one screen with three copy shapes. Their own subcommands rather than a flag, for
  // the reason every other state board has one -- a flag could not be named by the
  // comparison sheet or by a golden.
  const bool isBookError = std::strcmp(argv[1], "book_error") == 0;
  const bool isBookErrorUnreadable = std::strcmp(argv[1], "book_error_unreadable") == 0;
  const bool isBookErrorMemory = std::strcmp(argv[1], "book_error_memory") == 0;
  const bool isBookDetails = std::strcmp(argv[1], "book_details") == 0;
  const bool isSettings = std::strcmp(argv[1], "settings") == 0;
  const bool isSleep = std::strcmp(argv[1], "sleep") == 0;
  const bool isSleepIdle = std::strcmp(argv[1], "sleep_idle") == 0;
  const bool isSleepWaking = std::strcmp(argv[1], "sleep_waking") == 0;
  // design/SleepCover.dc.html and design/SleepCoverDetails.dc.html: the two modes
  // that put the book's own cover on the glass. Their own subcommands rather than a
  // flag on `sleep`, for the reason every other state board has one -- a flag could
  // not be named by the comparison sheet or by a golden.
  const bool isSleepCover = std::strcmp(argv[1], "sleep_cover") == 0;
  const bool isSleepCoverDetails = std::strcmp(argv[1], "sleep_cover_details") == 0;
  // design/SleepCoverWaking.dc.html: the WAKE over a cover, which is neither of the
  // two above and neither of the two waking/sleeping boards. `sleep_waking` is the
  // words over the dithered card with no picture; `sleep_cover` is the picture with
  // no words, because a full-bleed cover says "asleep" unaided. It cannot say
  // "waking", so this is the one place the cover keeps its badge -- and the one
  // sleep render that paints ONE pass, because a wake gets one waveform.
  const bool isSleepCoverWaking = std::strcmp(argv[1], "sleep_cover_waking") == 0;
  // design/LibraryOpening.dc.html. The SAME journey as `library` -- it is the same
  // screen, with the status line drawn over its hint bar the way the shell draws it
  // over a finished frame. Rendering it any other way would compare a board against
  // a path the device does not take.
  const bool isLibraryOpening = std::strcmp(argv[1], "library_opening") == 0;
  const bool isReaderMenu = std::strcmp(argv[1], "reader_menu") == 0;
  const bool isContents = std::strcmp(argv[1], "contents") == 0;
  const bool isHomeEmpty = std::strcmp(argv[1], "home_empty") == 0;
  const bool isHomeUnopened = std::strcmp(argv[1], "home_unopened") == 0;
  // design/HomeCharging.dc.html. Home with the cable in: the SAME view model as
  // `home`, with one flag set, because that is the only thing that differs on
  // the device. A second demo view-model would be a second place for the board's
  // content to live and to drift.
  const bool isHomeCharging = std::strcmp(argv[1], "home_charging") == 0;
  const bool isLibraryScrolled = std::strcmp(argv[1], "library_scrolled") == 0;
  const bool isReader = std::strcmp(argv[1], "reader") == 0;
  // The two styled specimens, each its own subcommand for the reason every other
  // state board has one: a flag on `reader` would mean the goldens and the comparison
  // sheet could not name them.
  const bool isChapterOpen = std::strcmp(argv[1], "reader_chapter_open") == 0;
  // The Reader WITH a way back. Reached by paging -- forward then back -- because
  // that is the transition that sets an anchor, and a state assigned directly would
  // pin the same pixels while proving nothing about the rule that produces them.
  const bool isAnchored = std::strcmp(argv[1], "reader_anchored") == 0;
  const bool isReaderList = std::strcmp(argv[1], "reader_list") == 0;
  // design/Typography.dc.html. The SAME journey as `reader_menu` -- the Reader, then
  // the menu over it -- and then the panel pushed on top, so the stack this renders
  // is the stack the device will have.
  //
  // IT PRESSES ITS WAY IN, over DOWN then CONFIRM on the menu, which is the route the
  // device takes. It could not before: the menu's `Typography` row was
  // `{"Typography", "", false, true}` -- not focusable, no action -- so DOWN SKIPPED
  // it (Focus::Gate refuses an unfocusable landing) and landed on `About this book`,
  // where CONFIRM pushes BookDetails. So this branch pushed the ScreenId directly and
  // said so, because a subcommand written as the two presses would have rendered the
  // WRONG SCREEN and reported success.
  //
  // The row is live now, so the presses are what runs, and the branch asserts the
  // focus and the top of the stack either side of them. That is what makes this a
  // NAVIGATION check again rather than a render-only one: the render did not move (it
  // is byte-identical to the golden the direct push blessed, which is the proof the
  // row does exactly what the push did), and what is newly covered is the row going
  // inert again -- which nothing on the desktop would otherwise notice.
  const bool isTypography = std::strcmp(argv[1], "typography") == 0;
  // design/Peek.dc.html. THE SAME JOURNEY AS `reader_menu` -- an App rooted at the
  // Reader with the panel pushed over it -- because App::render walks down to the
  // topmost non-overlay, paints it, veils it and paints each overlay above. Rendering
  // top().render alone is the mistake that paints a panel floating on white, and
  // nothing on the desktop can catch it: every other path here goes through
  // App::render.
  //
  // NO READER MENU UNDERNEATH, unlike its two siblings in this branch. On the device a
  // peek is reached from Contents, and the pop that opens it takes the menu AND
  // Contents off -- so the stack the board draws is Reader + Peek and nothing else.
  const bool isPeek = std::strcmp(argv[1], "peek") == 0;
  // design/BookEnd.dc.html. Its own subcommand rather than a state of `reader`, for
  // the reason every other state board has one: a flag could not be named by the
  // comparison sheet or by a golden.
  const bool isBookEnd = std::strcmp(argv[1], "book_end") == 0;
  // design/LowBattery.dc.html. THE READER, with the banner armed -- it is not a
  // screen of its own, it is the page with an inverted band drawn over its foot, so
  // this takes `reader`'s path exactly and adds one call. Its own subcommand for the
  // reason every other state board has one: a flag on `reader` could be named by
  // neither the comparison sheet nor a golden.
  const bool isLowBattery = std::strcmp(argv[1], "low_battery") == 0;
  // design/BatteryEmpty.dc.html. Nothing navigates here -- the shell paints it on the
  // way down -- so it is pushed directly, which is `sleep`'s model and for `sleep`'s
  // reason. It needs no priming: the board's copy lives in the screen's constructor.
  const bool isBatteryEmpty = std::strcmp(argv[1], "battery_empty") == 0;
  if (!isHome && !isSdMissing && !isApp && !isLibrary && !isLibraryActions &&
      !isDeleteConfirm && !isBookDetails && !isSettings && !isSleep && !isHomeEmpty &&
      !isHomeUnopened && !isHomeCharging && !isLibraryScrolled && !isReader && !isSleepIdle &&
      !isReaderMenu && !isContents && !isChapterOpen && !isReaderList && !isAnchored &&
      !isSleepWaking && !isLibraryOpening && !isTypography && !isPeek && !isSleepCover &&
      !isSleepCoverDetails && !isSleepCoverWaking && !isBookEnd && !isBookError &&
      !isBookErrorUnreadable && !isBookErrorMemory && !isLowBattery &&
      !isBatteryEmpty) {
    std::fprintf(stderr,
                 "unknown screen '%s' (expected 'home', 'sd_missing', 'library', "
                 "'library_actions', 'delete_confirm', 'book_details', 'settings', "
                 "'sleep', 'sleep_idle', 'home_empty', 'home_unopened', 'home_charging', "
                 "'library_scrolled', 'reader', 'reader_anchored', "
                 "'reader_chapter_open', 'reader_list', "
                 "'reader_menu', 'contents', 'typography', 'sleep_waking', "
                 "'sleep_cover', 'sleep_cover_details', 'sleep_cover_waking', "
                 "'library_opening', 'peek', 'book_end', 'book_error', "
                 "'book_error_unreadable', 'book_error_memory', 'low_battery', "
                 "'battery_empty' or "
                 "'app')\n",
                 argv[1]);
    return 3;
  }

  SimRamp ramp;
  if (!loadRamp(ramp)) return 1;
  const reader::FontSet& fonts = ramp.fonts;

  reader::QuietTheme theme;

  // The body face, for `reader` only. Its BYTES live here beside it for the same
  // reason SimRamp keeps the ramp's: ScalableFont borrows the buffer it was
  // initialised from and never copies it, so a vector that went out of scope
  // would leave it rasterising from freed heap.
  std::vector<uint8_t> bodyTtf;
  reader::ScalableFont body;
  // THE ITALIC IS A SECOND FILE AND A SECOND FACE, and its buffer has to outlive it
  // for the same reason the roman's does.
  std::vector<uint8_t> italicTtf;
  reader::ScalableFont italic;
  if (isReader || isReaderMenu || isChapterOpen || isReaderList || isAnchored ||
      isTypography || isPeek || isLowBattery) {
    bodyTtf = slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
    if (!body.init(bodyTtf.data(), bodyTtf.size(), reader::kBodyPpem)) {
      std::fprintf(stderr, "body face failed to load\n");
      return 1;
    }
    italicTtf = slurp(std::string(ASSETS_DIR) + "/built/literata_italic.ttf");
    if (!italic.init(italicTtf.data(), italicTtf.size(), reader::kBodyPpem)) {
      std::fprintf(stderr, "italic face failed to load\n");
      return 1;
    }
  }

  if (isReaderMenu || isTypography || isPeek) {
    // AN OVERLAY NEEDS ITS PARENT, so this goes through an App rooted at the Reader
    // rather than rendering one screen: App::render walks down to the topmost
    // non-overlay, paints it, then paints each overlay above -- and rendering
    // top().render alone is the mistake that paints a panel floating on white, which
    // nothing on the desktop can catch because every other path here goes through
    // App::render.
    reader::PageMetrics m;
    theme.readerMetrics(w, h, fonts, body, reader::Settings{}, m);
    m.italic = &italic;
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body);
    factory.setReaderItalic(&italic);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    factory.setContentsDemo();  // the menu's header comes from the same demo catalogue
    std::unique_ptr<reader::Screen> page = factory.create(reader::ScreenId::Reader);
    if (page == nullptr) {
      std::fprintf(stderr, "the factory refused ScreenId::Reader\n");
      return 1;
    }
    static_cast<reader::ReaderScreen*>(page.get())->completeIndex();
    reader::App app(std::move(page), factory);
    // NO READER MENU UNDERNEATH A PEEK -- see isPeek's own comment above. The other
    // two subcommands in this branch reach their overlay through the menu; a peek is
    // reached from Contents on the device, and the pop that opens it takes both the
    // menu and Contents off, so the stack here is Reader + Peek only.
    if (!isPeek) {
      if (!app.pushScreen(reader::ScreenId::ReaderMenu)) {
        std::fprintf(stderr, "the factory refused ScreenId::ReaderMenu\n");
        return 1;
      }
    }
    if (isPeek) {
      // THE PANEL'S OWN COLUMN, which is the whole design: inset, so narrower, so its
      // text re-wraps -- and re-wrapped text paginates differently, which is why the
      // panel shows chapter and percent rather than a page number.
      reader::PageMetrics pm;
      theme.peekMetrics(w, h, fonts, body, reader::Settings{}, pm);
      pm.italic = &italic;
      factory.setPeekMetrics(pm);
      factory.setPeekDemo();
      if (!app.pushScreen(reader::ScreenId::Peek)) {
        std::fprintf(stderr, "the factory refused ScreenId::Peek\n");
        return 1;
      }
    }
    if (isTypography) {
      // THE DEFAULT SETTINGS, WHICH ARE NOW THE BOARD'S TOO. This branch overrode
      // bodyPpem to 38 and re-inited the face at it, because the board's Size row
      // said `18 PT` while its preview was set at `font-size: 32px` -- and the
      // firmware cannot render both. The BOARD was the thing that disagreed with
      // itself, and it is the board that changed: at ppem 38 the fixed box holds
      // three of the specimen's four lines, so the preview cut off mid-sentence at
      // "seems to be thrown" with ~85px of empty box under it, which is the worst
      // state the screen can produce and the one this render would have pinned.
      //
      // So there is nothing to prime: `reader::Settings{}` is what the factory
      // already holds and kBodyPpem is what the face above is already inited at.
      // 18 PT remains one press away on the device, and whether it should be the
      // DEFAULT is a separate open question (roadmap:1269).
      //
      // PRESSED, not pushed -- see isTypography's own comment. DOWN from Contents
      // reaches Typography, and CONFIRM there opens the panel.
      app.dispatch({reader::Button::Down, reader::PressKind::Short});
      if (static_cast<const reader::ReaderMenuScreen&>(app.top()).vm().focusedRow !=
          reader::ReaderMenuScreen::kTypography) {
        std::fprintf(stderr, "DOWN on the reader menu did not reach the Typography row\n");
        return 1;
      }
      app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
      if (app.top().id() != reader::ScreenId::Typography) {
        std::fprintf(stderr, "CONFIRM on the Typography row did not open the panel\n");
        return 1;
      }
    }
    if (!renderPassesToPng(
            [&](reader::Framebuffer& fb, reader::Plane pl) { app.render(fb, fonts, theme, pl); },
            app.top().fidelity(), w, h, argv[2]))
      return 1;
    if (isTypography) {
      const auto& t = static_cast<const reader::TypographyScreen&>(app.top());
      std::printf("wrote %s (%dx%d) typography, focus %d, %s, ppem %d\n", argv[2], w, h, t.focus(),
                  t.vm().justify ? "justified" : "ragged", t.settings().bodyPpem);
      return 0;
    }
    if (isPeek) {
      const auto& p = static_cast<const reader::PeekScreen&>(app.top());
      std::printf("wrote %s (%dx%d) peek over the page, %s, %d lines\n", argv[2], w, h,
                  p.vm().where.c_str(), static_cast<int>(p.page().lines.size()));
      return 0;
    }
    std::printf("wrote %s (%dx%d) reader menu over the page\n", argv[2], w, h);
    return 0;
  }

  if (isSleepCover || isSleepCoverDetails || isSleepCoverWaking) {
    // NOT THROUGH THE App, AND NOT THROUGH THE FACTORY, and both halves of that are
    // the device's own shape rather than a shortcut.
    //
    // The shell paints the sleep screen with `paintSleepScreen`, which bypasses
    // `App` entirely -- pushing it would make the next wake RESTORE INTO IT, so
    // "press power" would give back "asleep, press power to wake". Sleep is a screen
    // nothing navigates to.
    //
    // And the factory has no CoverSource to hand over, correctly: it is the
    // NAVIGATION catalogue, so a screen nothing can navigate to has no business
    // teaching it about a cover cache. The three sleep subcommands above still go
    // through it because they need only a demo view model, which is exactly what it
    // is for.
    //
    // THE CARD'S COPY IS demoSleepVm()'s, WHICH IS design/Sleep.dc.html'S. Both
    // boards say so in as many words: SleepCoverDetails is "Sleep.dc.html with its
    // background replaced, and nothing else", so anything the card differs by is a
    // defect. Spelling it out here would be a second copy of the board's own words
    // and a place for the two to drift.
    BoardCover cover;
    if (!loadBoardCover(cover, w, h)) return 1;

    reader::SleepViewModel vm = reader::demoSleepVm();
    vm.shows = isSleepCoverDetails ? reader::SleepShows::CoverAndDetails : reader::SleepShows::Cover;
    if (isSleepCoverWaking) {
      // BOTH FIELDS, as the shell sets both and as screens.cpp's waking demo does:
      // the note is what the screen SAYS, `waking` is which screen this IS -- and
      // only the second reaches the badge rule that COVER mode would otherwise
      // silence. Setting the note alone would render design/SleepCover.dc.html and
      // report it as this board.
      vm.note = reader::kStatusWaking;
      vm.waking = true;
    }
    reader::SleepScreen scr(std::move(vm), &cover);
    // A COVER IS THE ONE THING ON THIS DEVICE THAT NEEDS FOUR LEVELS, so the two
    // SLEEPING boards take the three-pass path -- asserted rather than assumed,
    // because a Mono render of them would be a plausible-looking sheet measuring the
    // wrong pipeline. See SleepScreen::fidelity.
    //
    // THE WAKING BOARD ASSERTS THE SAME THING AND THEN IGNORES IT, WHICH IS THE
    // DEVICE'S OWN SHAPE. fidelity() answers Grayscale here too -- there is a cover
    // and the mode is COVER, and nothing about waking changes that -- but the wake
    // paint in shell/src/main.cpp does not CONSULT it: fidelity() is read by
    // renderTop() for an App-owned screen and by paintSleepScreen for the sleep
    // sequence, and the wake is neither. It renders Plane::Bw and calls showOnePass
    // itself, because a wake gets one waveform. So the assertion stays (it pins that
    // the screen was built the same way) and the render below is one pass, and the
    // two together are exactly what the shell does.
    if (scr.fidelity() != reader::Fidelity::Grayscale) {
      std::fprintf(stderr, "the cover sleep screen did not ask for Grayscale\n");
      return 1;
    }
    if (isSleepCoverWaking) {
      // ONE PASS, Plane::Bw -- which IS the Msb plane, so this is the sleep screen's
      // own picture at two levels rather than a different image. Fidelity::Mono is
      // how renderPassesToPng spells that; it is NOT a claim about scr.fidelity(),
      // which is asserted Grayscale two lines up.
      if (!renderPassesToPng(
              [&](reader::Framebuffer& fb, reader::Plane p) { scr.render(fb, fonts, theme, p); },
              reader::Fidelity::Mono, w, h, argv[2]))
        return 1;
    } else if (!renderToPng(scr, fonts, theme, w, h, argv[2])) {
      return 1;
    }
    std::printf("wrote %s (%dx%d) sleep, %s, over design/assets/sleep-cover-%dx%d.png\n", argv[2],
                w, h,
                isSleepCoverWaking  ? "waking over the cover, one pass"
                : isSleepCover      ? "the cover alone"
                                    : "the cover behind the card",
                w, h);
    return 0;
  }

  if (isContents) {
    // A full screen, so it renders on its own -- and the row count comes from the
    // theme, as the Library's does, because a list told nothing renders empty.
    reader::DemoScreenFactory factory;
    // ASKED FOR, as the Reader's demo is: the factory refuses a Contents that nothing
    // primed, so a device that failed to read a real one shows no list rather than the
    // board's.
    factory.setContentsDemo();
    factory.setContentsVisibleRows(theme.contentsVisibleRows(h, fonts));
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Contents);
    if (scr == nullptr) {
      std::fprintf(stderr, "the factory refused ScreenId::Contents\n");
      return 1;
    }
    if (!renderToPng(*scr, fonts, theme, w, h, argv[2])) return 1;
    const auto& c = static_cast<const reader::ContentsScreen&>(*scr);
    std::printf("wrote %s (%dx%d) %d entries, %s, focus %d\n", argv[2], w, h, c.rowCount(),
                c.sectioned() ? "sectioned" : "flat", c.focus());
    return 0;
  }

  if (isBookEnd) {
    // A full screen, not an overlay -- the board has no veil and no panel, and it draws
    // its own header band and hint bar -- so it renders on its own with no App beneath.
    reader::DemoScreenFactory factory;
    // ASKED FOR, as the Reader's and Contents' demos are: the factory refuses a BookEnd
    // that nothing primed rather than substituting, so a shell that failed to hand over
    // the facts shows nothing rather than another book's title.
    factory.setBookEndDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::BookEnd);
    if (scr == nullptr) {
      std::fprintf(stderr, "the factory refused ScreenId::BookEnd\n");
      return 1;
    }
    if (!renderToPng(*scr, fonts, theme, w, h, argv[2])) return 1;
    const auto& be = static_cast<const reader::BookEndScreen&>(*scr);
    std::printf("wrote %s (%dx%d) '%s' / '%s' / '%s', focus %d\n", argv[2], w, h,
                be.vm().title.c_str(), be.vm().byline.c_str(), be.vm().meta.c_str(),
                be.focus());
    return 0;
  }

  if (isChapterOpen || isReaderList) {
    // THE SAME PATH AS `reader`, with the demo swapped -- through the real
    // ReaderScreen and the real Theme::readerMetrics, so the PNG is laid out by the
    // arithmetic the device runs. What these two add is content that exercises every
    // BlockKind: a heading, an italic inset blockquote, prose with inline emphasis,
    // and a hanging-indent list.
    reader::PageMetrics m;
    theme.readerMetrics(w, h, fonts, body, reader::Settings{}, m);
    m.italic = &italic;
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body);
    factory.setReaderItalic(&italic);
    factory.setReaderMetrics(m);
    factory.setReaderStyleDemo(isChapterOpen
                                   ? reader::DemoScreenFactory::ReaderStyleDemo::ChapterOpen
                                   : reader::DemoScreenFactory::ReaderStyleDemo::List);
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    if (scr == nullptr) {
      std::fprintf(stderr, "the factory refused ScreenId::Reader\n");
      return 1;
    }
    static_cast<reader::ReaderScreen*>(scr.get())->completeIndex();
    if (!renderToPng(*scr, fonts, theme, w, h, argv[2])) return 1;
    const auto& rd = static_cast<const reader::ReaderScreen&>(*scr);
    std::printf("wrote %s (%dx%d) page %d/%d, %zu lines, column %dx%d, %d%%\n", argv[2], w,
                h, rd.vm().page, rd.vm().pageTotal, rd.page().lines.size(), m.columnW,
                m.columnH, rd.vm().progressPercent);
    return 0;
  }

  if (isAnchored) {
    // The same path as `reader`, with two presses on the end.
    reader::PageMetrics m;
    theme.readerMetrics(w, h, fonts, body, reader::Settings{}, m);
    m.italic = &italic;
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body);
    factory.setReaderItalic(&italic);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    if (scr == nullptr) {
      std::fprintf(stderr, "the factory refused ScreenId::Reader\n");
      return 1;
    }
    auto* rd = static_cast<reader::ReaderScreen*>(scr.get());
    rd->completeIndex();
    // THE SIDES PAGE. Button::Right/Left ARE the side buttons -- the shell's mapping
    // is crossed, so read it rather than the names. Forward, then back: the backward
    // turn is what sets the anchor to the page being left.
    rd->onEvent({reader::Button::Right, reader::PressKind::Short});
    rd->onEvent({reader::Button::Left, reader::PressKind::Short});
    if (rd->vm().anchorLabel.empty()) {
      std::fprintf(stderr, "no anchor after paging forward and back\n");
      return 1;
    }
    if (!renderToPng(*scr, fonts, theme, w, h, argv[2])) return 1;
    std::printf("wrote %s (%dx%d) page %d/%d, anchor \"%s\"\n", argv[2], w, h, rd->vm().page,
                rd->vm().pageTotal, rd->vm().anchorLabel.c_str());
    return 0;
  }

  if (isReader || isLowBattery) {
    // Through the real ReaderScreen, and the metrics through the real
    // Theme::readerMetrics -- so the PNG is laid out by exactly the arithmetic the
    // device runs, rather than by a column this file picked. Reader declares
    // Fidelity::Grayscale, so renderToPng renders three planes and composes them:
    // this is the first screen in the project whose golden is not a 1-bit frame.
    reader::PageMetrics m;
    theme.readerMetrics(w, h, fonts, body, reader::Settings{}, m);
    m.italic = &italic;
    reader::DemoScreenFactory factory;
    factory.setReaderBody(&body);
    factory.setReaderItalic(&italic);
    factory.setReaderMetrics(m);
    factory.setReaderDemo();
    std::unique_ptr<reader::Screen> scr = factory.create(reader::ScreenId::Reader);
    if (scr == nullptr) {
      std::fprintf(stderr, "the factory refused ScreenId::Reader\n");
      return 1;
    }
    // THE SETTLED STATE, as the goldens render and as the board shows. A chapter
    // opens with its page count unknown -- the footer draws an em dash for it -- and
    // the device fills it in within five seconds, inside the refinement. A simulator
    // that showed the transient state would put `1 / —` in the comparison sheet
    // against a board that says `53 / 890`, and would disagree with the goldens.
    static_cast<reader::ReaderScreen*>(scr.get())->completeIndex();
    // design/LowBattery.dc.html's own 5%. The board's number, so the sheet compares
    // the same string the design states.
    //
    // 5 IS AN X3 SPECIMEN, not the X4's value: the X4's ADC reports 10% notches so
    // its banner only ever shows 10. The board says 5 and this matches the board;
    // the fit at 480px was measured against `BATTERY LOW - 10%`, which is longer.
    //
    // AFTER completeIndex, and the ordering is the settled-state rule rather than a
    // dependency -- the band is drawn OVER a page the count does not move.
    if (isLowBattery) static_cast<reader::ReaderScreen*>(scr.get())->setBatteryLow(5);
    if (!renderToPng(*scr, fonts, theme, w, h, argv[2])) return 1;
    const auto& rd = static_cast<const reader::ReaderScreen&>(*scr);
    std::printf("wrote %s (%dx%d) page %d/%d, %zu lines, column %dx%d\n", argv[2], w, h,
                rd.vm().page, rd.vm().pageTotal, rd.page().lines.size(), m.columnW,
                m.columnH);
    return 0;
  }

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
  // THE SCROLLED LIBRARY NEEDS A LONGER LIST, not a different screen: the rail's
  // thumb is visible/total and its position is first/total, so a seven-item list
  // -- which fits -- cannot produce a rail at all. This is the ONLY state in which
  // the rail is compared against its board, and until it existed the rail was
  // checked by unit tests and by nothing that looked at a pixel.
  if (isLibraryScrolled) factory.setLibraryItems(reader::demoLibraryScrolledItems());
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
  // Both no-reading-column variants are a different ROOT, not a different
  // navigation: they are Home with nothing to continue, so there is no journey that
  // reaches either -- the card is what decides, and on the desktop that is a choice
  // of view model.
  reader::HomeViewModel homeVm = isHomeEmpty      ? reader::demoHomeEmptyVm()
                                 : isHomeUnopened ? reader::demoHomeUnopenedVm()
                                                  : reader::demoHomeVm();
  if (isHomeCharging) homeVm.batteryCharging = true;
  reader::App app(
      std::make_unique<reader::HomeScreen>(std::move(homeVm), reader::demoHomeTargets()),
      factory);

  if (isLibraryScrolled) {
    // Into the Library, DOWN PAST the board's focused row, then back up one.
    //
    // Not 12 Downs, which is the obvious thing and gives the wrong window:
    // ScrollWindow scrolls only as far as it must to keep the focus visible, so
    // arriving at row 12 from above lands it on the window's BOTTOM edge -- rows
    // 6..12, focus last. The board draws rows 7..13 with the focus sixth of seven,
    // which is the state after going one further and coming back: the window has
    // already moved and the focus steps back inside it.
    //
    // Worth the extra press rather than editing the board, because both states are
    // real and the board's is the more useful illustration of a rail -- a thumb
    // with list on both sides of it. And doing it by pressing is what proves the
    // window the board draws is one ScrollWindow actually produces.
    for (const reader::InputEvent& ev : libraryEntry(13)) app.dispatch(ev);
    app.dispatch({reader::Button::Up, reader::PressKind::Short});
  }
  if (isLibrary || isLibraryOpening) {
    // Home's first row is LIBRARY, so one Confirm opens it; then the board's own
    // focus, which is its second row. Reached by pressing rather than by
    // assignment, so the render pins the navigation too.
    //
    // LibraryOpening takes the IDENTICAL journey, because it is the identical
    // screen: the only difference is the status line drawn over the finished frame,
    // which is exactly how the device differs. A second journey here would let the
    // two boards drift apart in the one way the comparison could not see.
    for (const reader::InputEvent& ev : libraryEntry()) app.dispatch(ev);
  }
  if (isBookError || isBookErrorUnreadable || isBookErrorMemory) {
    // The board draws the LIBRARY under the veil with Dubliners focused -- the sixth
    // row, which is the same row the overlay boards focus and the same file the
    // dialog's paragraph names. Reached by pressing, as every other parent is.
    for (const reader::InputEvent& ev : libraryEntry(5)) app.dispatch(ev);
    // NOT reached by pressing, for the sleep screen's reason: no gesture on a Library
    // row raises this dialog. The SHELL raises it, when the open it tried refuses --
    // so pushing it directly is the honest model of what happens on the device.
    factory.setBookErrorFacts(
        isBookErrorMemory ? reader::demoBookErrorMemoryFacts()
                          : isBookErrorUnreadable ? reader::demoBookErrorUnreadableFacts()
                                                  : reader::demoBookErrorFacts());
    if (!app.pushScreen(reader::ScreenId::BookError)) {
      std::fprintf(stderr, "the factory refused ScreenId::BookError\n");
      return 1;
    }
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
  // The idle variant is the same screen with nothing to show, so it takes the same
  // direct push -- the factory picks which view model.
  if (isSleepIdle) factory.setSleepIdle();
  if (isSleepWaking) factory.setSleepWaking();
  if (isSleep || isSleepIdle || isSleepWaking) {
    // NOT reached by pressing: nothing navigates to the sleep screen, the idle
    // timer or the power button puts the device there. So it is pushed directly,
    // which is the honest model -- and it is why this screen has no journey to
    // pin the way Library's and Settings' renders do.
    if (!app.pushScreen(reader::ScreenId::Sleep)) {
      std::fprintf(stderr, "the factory refused ScreenId::Sleep\n");
      return 1;
    }
  }
  if (isBatteryEmpty) {
    // Sleep's model, for Sleep's reason: nothing navigates here. The shell paints
    // this one directly on the way down -- a pushed BatteryEmpty would be the screen
    // the next wake restores INTO -- so there is no journey to pin, only a render.
    if (!app.pushScreen(reader::ScreenId::BatteryEmpty)) {
      std::fprintf(stderr, "the factory refused ScreenId::BatteryEmpty\n");
      return 1;
    }
  }
  for (const reader::InputEvent& ev : events) app.dispatch(ev);

  // Through App::render, not top().render: with an overlay on the stack the top
  // screen alone is a panel floating on white, and one paint path is what keeps
  // the simulator, the goldens and the shell from disagreeing about that.
  if (isLibraryOpening) {
    // The shell's own sequence: App::render fills the frame, then the status bar is
    // drawn OVER the hint bar it replaces. No screen knows it happened, which is why
    // there is no view-model flag to set here.
    if (!renderPassesToPng(
            [&](reader::Framebuffer& fb, reader::Plane pl) {
              app.render(fb, fonts, theme, pl);
              reader::drawStatusBar(fb, fonts, reader::kStatusOpening, pl);
            },
            app.top().fidelity(), w, h, argv[2]))
      return 1;
    std::printf("wrote %s (%dx%d) library, opening a book\n", argv[2], w, h);
    return 0;
  }
  if (!renderAppToPng(app, fonts, theme, w, h, argv[2])) return 1;
  std::printf("wrote %s (%dx%d) screen=%d depth=%d\n", argv[2], w, h,
              static_cast<int>(app.top().id()), app.depth());
  return 0;
}
