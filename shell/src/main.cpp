#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <XteinkDetect.h>

#include <memory>
#include <optional>
#include <string>

#include "font_body400.h"
#include "font_body500.h"
#include "font_display700.h"
#include "font_label400.h"
#include "font_label500.h"
#include "font_meta400.h"
#include "font_meta500.h"
#include "font_title700.h"
#include "font_value500.h"
#include "font_value700.h"
#include "input_task.h"
#include "reader/app.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/input.h"
#include "reader/json.h"
#include "reader/power.h"
#include "reader/refresh.h"
#include "reader/screen_home.h"
#include "reader/screen_sd_missing.h"
#include "reader/screens.h"
#include "reader/settings.h"
#include "reader/text.h"  // reader::Plane
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"
#include "sd_fs.h"
#include "sd_selftest.h"
#include "session.h"

// Xteink display SPI pins. Shared by X3 and X4; MISO is shared with the SD card.
constexpr int8_t EPD_SCLK = 8, EPD_MOSI = 10, EPD_CS = 21, EPD_DC = 4, EPD_RST = 5,
                 EPD_BUSY = 6, SPI_MISO = 7;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

// THE THREE KNOBS THAT USED TO BE COMPILED IN.
//
// `kSleepAfterMs`, `kFullRefreshEvery` and `kFullOnTransition` were constants
// here through 2B. They are fields of reader::Settings now, read from
// /.reader/settings.json at boot, and the struct's DEFAULTS are the values that
// were compiled in -- so a device with no card, or with no settings file on the
// card, behaves exactly as it did. There is one copy of each number, in
// core/include/reader/settings.h, and gRefresh and gIdle below are constructed
// from it.
//
// What the numbers mean, and why they are what they are, stays here: the
// reasoning is about this panel and this driver, which is the shell's subject.
//
// SLEEP AFTER: five minutes idle. 0 means never sleep, which is a legitimate
// choice and the right one while a transfer is running, so the settings loader
// does not clamp it up to its floor.
//
// PERIODIC FULL REFRESH CADENCE: DISABLED, matching the reference firmware,
// which schedules no periodic full refresh in its UI at all.
//
// A cadence exists to clear the residue differential refreshes leave behind, and
// keeping one was the cautious choice -- but at 1-in-15 it put a black flash on
// an arbitrary navigation, which reads as MORE random than the transition flash
// it replaced, and unpredictable flashing was the complaint. Two things make the
// risk acceptable: CrossInk ships this way on this hardware, and the ink
// accumulation this project actually observed was caused by a missing grayscale
// settle pass, on a path chrome no longer takes at all -- not by FAST refreshes.
//
// If ghosting does appear, this number is the whole fix: set it to 15 or 20 --
// and it is a settings file away now rather than a rebuild.
// Watch for a screen that gradually stops being readable with no obvious cause.
//
// FULL ON TRANSITION: whether a screen change forces a FULL refresh. TRUE, and
// this is a deliberate divergence from the reference firmware.
//
// CrossInk does not do it on this panel: ScreenTransitionRefresh::modeFor returns
// FULL only for `screenChanged && !deviceIsX3()`, and its list/menu screens call
// displayBuffer() with no argument, whose default is FAST_REFRESH. So it avoids
// the flash and accepts the ghosting -- and the ghosting is observable, reported
// on its settings screen on this device.
//
// The distinction that matters is not "flash" versus "no flash". A flash on a
// SCREEN CHANGE is expected behaviour on an e-reader -- Kindle and Kobo both do
// it -- because a differential update there has a whole screen of stale content
// to ghost through. A flash on a focus move inside one screen is a defect. Those
// are separate settings here, so we take FULL on transitions and NO periodic
// cadence (see above), which is neither firmware's behaviour and is better than
// both.
//
// Applies to pop as well as push, deliberately: leaving Settings back to Home is
// exactly the case where the settings list would ghost onto Home, so treating
// only the outbound direction as a transition would fix half the problem.
//
// Cost, measured on the X3: a transition takes the 693 ms GC waveform instead of
// the 389 ms DU, so ~825 ms against ~520 ms. Focus moves are untouched. That is
// the right place to spend it -- screens change far less often than a focus does.

// Input-settle window before a repaint. ZERO, deliberately. NOT a setting: there
// is nothing here for a user to have a preference about, and no board row for it.
//
// It was 90 ms, added when a paint cost 1363 ms and a burst of presses cost N
// times that. A paint is now a single FAST waveform, so the insurance is worth
// far less and the latency costs far more: 90 ms was being added to EVERY press,
// including isolated ones, which is pure delay against a reference firmware that
// adds none.
//
// Bursts still coalesce, by the mechanism that was always doing the real work:
// the drain loop dispatches every queued event before painting, so presses that
// land DURING a paint are still merged into the next one. The settle window only
// ever caught presses landing in the gap between paints, and at a human press
// rate of ~150 ms it could not merge those without being long enough to feel.
constexpr uint32_t kCoalesceMs = 0;

// millis() of the last button transition, for the coalescing window. Starts at 0
// so the first paint in setup() is never deferred.
static uint32_t gLastInputMs = 0;

// Everything the render needs has to outlive setup(), so it lives here rather
// than on setup()'s stack -- but the frame stays heap-allocated behind
// unique_ptr on purpose. A file-scope Framebuffer would allocate during static
// init, before the largest-block check in setup() could run, and a vector that
// cannot allocate under -fno-exceptions is an abort() boot loop with no
// diagnostic. This project has already lost a boot to exactly that.
//
// ONE frame, not two. It is logically portrait (what the screens draw against)
// and physically landscape (what the panel is handed), because it is built with
// Rotation::Ccw -- see the allocation in setup().
static std::unique_ptr<reader::Framebuffer> gFrame;
// FontSet owns nothing: every Font it holds is a zero-copy view into a blob the
// caller supplies. The embedded kFont* arrays have static storage, so they
// outlive the set -- but nothing here may ever hand load() a scope-limited copy.
static std::optional<reader::FontSet> gFonts;
static reader::QuietTheme gTheme;
// The screen catalogue is the one from core/, shared with the simulator. A
// shell-local copy would drift from it on row lists and titles, and the drift
// would be invisible because each half would keep passing its own checks.
static reader::DemoScreenFactory gFactory;
static std::unique_ptr<reader::App> gApp;
static reader::PressRecognizer gPresses;
// The settings as they will be applied. Constant-initialised to the struct's
// defaults -- the numbers that were compiled in through 2B -- so gRefresh and
// gIdle below are correct before setup() runs and stay correct if there is no
// card, no settings file, or a settings file this build refuses.
static reader::Settings gSettings;
// Constructed FROM gSettings, declared above it in this same translation unit, so
// there is no second copy of the defaults to drift. Both take the loaded values
// through setters in loadAndApplySettings(); a constructor argument could not,
// because these are alive long before the card is mounted.
static reader::RefreshPolicy gRefresh(gSettings.fullRefreshEvery, gSettings.fullOnTransition);
static reader::IdleTimer gIdle(gSettings.sleepAfterMs);
static InputManager gInput;
// The card. One instance: SDCardManager is a singleton underneath, so a second
// SdFileSystem would address the same volume with its own idea of whether it is
// mounted.
static SdFileSystem gSd;
// Did SDCardManager::begin() ever return true this boot? It opens with
// `if (initialized) return true;` and the SPI path exposes no end()/unmount(), so
// after one success a later begin() reports success WITHOUT touching the
// hardware. That is the whole reason bringUpStorage() below does not trust it.
static bool gSdBeganOnce = false;

// Bring-up instrumentation. Serial here is native USB CDC, so the port
// re-enumerates when the app starts and anything printed in the first second is
// lost to the host. Every stage is announced and the last one reached is
// repeated from loop(), so a hang can be located by attaching at any time.
static const char* stage = "boot";
static void mark(const char* s) {
  stage = s;
  Serial.printf("[stage] %s\n", s);
  Serial.flush();
}

// --- Storage -------------------------------------------------------------
//
// Mount the card and say, honestly, whether the filesystem is usable.
//
// `mount()` alone is not that answer. SDCardManager::begin() opens with
// `if (initialized) return true;` and the SPI path exposes no end() or unmount(),
// so once it has succeeded it keeps succeeding whether or not the card is still
// in the slot. A retry that trusted it would report success, replace the
// SD-missing screen with Home, and then fail on the first read -- and a RETRY
// button that lies is worse than one that stays put, because the user stops
// believing the screen.
//
// So a mount is only accepted once probe() has actually read the root directory
// and mounted() agrees. probe() is not a card-detect (see sd_fs.cpp: it can be
// satisfied from SdFat's sector cache, and there is no card-detect GPIO in the
// board profiles at all), but it is real traffic to the card, which is strictly
// more than begin() promises after the first call.
//
// `why` is "boot" or "retry", and it is in every line here on purpose: the two
// paths differ only in what they mean, so a serial log without it is ambiguous.
static bool bringUpStorage(const char* why) {
  const bool begun = gSd.mount();  // logs "[sd] mount ok" / "... FAILED"
  if (!begun) {
    Serial.printf("[sd] %s: no usable storage\n", why);
    Serial.flush();
    return false;
  }
  const bool wasFirst = !gSdBeganOnce;
  gSdBeganOnce = true;
  if (gSd.probe() && gSd.mounted()) {
    Serial.printf("[sd] %s: storage usable (mount confirmed by a root-directory read)\n", why);
    Serial.flush();
    return true;
  }
  // begin() said yes and the card would not answer. Two ways to get here, and
  // only one of them is recoverable:
  //   * first begin() of this boot: a card that mounts but cannot be read. Worth
  //     retrying -- reseating it may fix it.
  //   * a later begin(): the short-circuit above. The card mounted once and is
  //     now gone, and there is no unmount to undo that, so nothing this firmware
  //     can do will recover it. The screen must stay and the log must say why,
  //     rather than the retry cycling forever on a lie.
  if (wasFirst) {
    Serial.printf("[sd] %s: begin() succeeded but the card would not answer a directory "
                  "read; not treating storage as usable\n",
                  why);
  } else {
    Serial.printf("[sd] %s: begin() returned true WITHOUT touching the card -- it "
                  "short-circuits on its own `initialized` flag -- and the card is not "
                  "answering. A card that mounted once and was then pulled cannot be "
                  "re-mounted without a REBOOT; staying on the SD-missing screen\n",
                  why);
  }
  Serial.flush();
  return false;
}

// Why loadSettings() said no. It answers with one bool over six distinguishable
// causes, and this log line is the only way a user ever learns their hand-edited
// file was rejected rather than applied -- so the shell asks the same questions
// again, in the same order, and names the first one that fails.
//
// `defaulted` matters as much as the reason, because the two halves of that list
// leave the device in different states: the first four throw the file away and
// run on defaults, while the last two KEEP the file and correct one field. Saying
// "defaulted" for a clamped value would be a false statement about every other
// setting in the file.
//
// Called ONLY on failure, so the happy path pays nothing; the cost is one more
// small read on the shared bus when something is already wrong.
struct SettingsVerdict {
  const char* reason;
  bool defaulted;
};

static SettingsVerdict settingsFailure(reader::FileSystem& fs) {
  // First, because every question below answers "no" on an unmounted filesystem
  // and "there is no file" would be the wrong story to tell about a missing card.
  if (!fs.mounted()) return {"no usable storage, so there was nothing to read", true};
  if (!fs.exists(reader::kSettingsPath))
    return {"no file yet (first boot, or nothing has saved one)", true};
  std::string text;
  if (!fs.readAll(reader::kSettingsPath, text)) return {"the file exists but would not read", true};
  reader::JsonObject o;
  if (!o.parse(text))
    return {"not parseable flat JSON -- hand-edited, or a write lost power part way", true};
  int64_t version = 0;
  if (!o.getInt("version", version)) return {"no `version` key", true};
  if (version != reader::kSettingsVersion)
    return {"a `version` this build does not know", true};
  return {"a value was out of range and was clamped, or was the wrong JSON type and was "
          "ignored; every other field in the file still applies",
          false};
}

// Read the settings and apply them. Safe with an unmounted filesystem: every
// FileSystem method fails when mounted() is false, so loadSettings() falls back
// to defaults and this reports exactly that.
static void loadAndApplySettings() {
  const bool ok = reader::loadSettings(gSd, gSettings);
  if (ok) {
    Serial.printf("[boot] settings loaded from %s\n", reader::kSettingsPath);
  } else {
    const SettingsVerdict v = settingsFailure(gSd);
    Serial.printf("[boot] settings %s: %s\n", v.defaulted ? "DEFAULTED" : "CORRECTED", v.reason);
  }
  gRefresh.setCadence(gSettings.fullRefreshEvery);
  gRefresh.setFullOnTransition(gSettings.fullOnTransition);
  gIdle.setTimeout(gSettings.sleepAfterMs);
  Serial.printf("[boot] settings in force: sleepAfterMs=%lu fullRefreshEvery=%d "
                "fullOnTransition=%d\n",
                (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
                (int)gSettings.fullOnTransition);
  Serial.flush();
}

// --- The app, and the session record -------------------------------------

// Build the app with Home as its root, replacing whatever was there.
//
// App has no "replace the root", and a successful retry cannot PUSH Home: the
// SD-missing screen is the root in that state, so Home would be at depth 2 and
// Back would pop to a screen whose message is no longer true. A fresh App is the
// straightforward answer, and the long-press mask has to be re-synced with it --
// the mask is PressRecognizer's, not the App's, so a new stack whose top binds
// different holds leaves the recognizer bound to the old screen's.
static void buildHomeApp() {
  gApp = std::make_unique<reader::App>(
      std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
      gFactory);
  gPresses.setLongPressable(gApp->longPressable());
}

// Store where the user is, so a wake can put them back. Cheap to call after every
// dispatch: saveSession() skips an identical rewrite, so navigating back and
// forth does not grind the NVS partition.
//
// FOCUS IS ALWAYS 0, and that is deliberate rather than unfinished. reader::Screen
// exposes id/fidelity/longPressable/onEvent/render and no focus accessor, so there
// is no way to read the focus of the screen on top without adding virtuals to
// every screen -- and the only multi-item screen where a restored focus would be
// visible is Library, which does not exist until 2C-2. Adding two virtuals now to
// carry a value nothing can produce is speculative; 2C-2 wires it when there is a
// concrete need, and the record already has the field.
static void saveWhereWeAre() {
  Session s;
  s.screen = gApp->top().id();
  s.focus = 0;
  saveSession(s);
}

// The SD-missing screen's RETRY, which App latched for us because mounting is not
// core/'s to do.
static void handleRetry() {
  // Clear the latch FIRST, so a failed attempt cannot re-fire on every loop.
  gApp->clearRetryRequest();
  mark("sd-retry");
  if (!bringUpStorage("retry")) {
    // Nothing changes on glass: the message is still true, App::dispatch
    // deliberately does not mark a Retry dirty, and spending a full refresh to
    // redraw an identical screen would read as the button having done something.
    // bringUpStorage() has already logged which failure this was -- in
    // particular whether a reboot is now required.
    return;
  }
  loadAndApplySettings();  // the settings live on the card that just appeared
  buildHomeApp();          // and Home replaces the root; see buildHomeApp()
  mark("sd-retry-ok");
}

// One binary drives both Xteink models, and the panel controller varies by
// production batch, so the running firmware has to work out what it is on
// before touching the display:
//   1. I2C fingerprint for the X3-only peripherals (fuel gauge / RTC / IMU)
//   2. select the matching board profile
//   3. probe the display bus, because newer X3 units carry a UC8279d in place
//      of the UC8253 and the two need different drivers
// Skipping step 3 is what made the first paint hang: a UC8279 acknowledges the
// UC8253 power-on but never completes its refresh waveform.
static void detectAndSelectBoard() {
  uint8_t s1 = 0, s2 = 0;
  const auto verdict = freeink::detectXteinkVerdict(&s1, &s2);
  const bool isX3 = (verdict == freeink::XteinkVerdict::X3Confirmed);
  Serial.printf("[detect] i2c verdict=%s (pass scores %u/%u) -> %s\n",
                verdict == freeink::XteinkVerdict::X3Confirmed    ? "X3Confirmed"
                : verdict == freeink::XteinkVerdict::X4Confirmed  ? "X4Confirmed"
                                                                  : "Inconclusive",
                s1, s2, isX3 ? "X3" : "X4");

  BoardConfig::selectDevice(isX3 ? BoardConfig::Board::XteinkX3
                                 : BoardConfig::Board::XteinkX4);

  const bool promoted = freeink::applyXteinkDisplayController();
  const auto& diag = freeink::getXteinkDisplayProbeDiag();
  Serial.printf("[detect] controller probe valid=%d promoted=%d "
                "ver=%02X %02X %02X %02X %02X flg=%02X\n",
                diag.valid, promoted, diag.ver[0], diag.ver[1], diag.ver[2],
                diag.ver[3], diag.ver[4], diag.flg);

  if (isX3 && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
    Serial.printf("[detect] promoted profile to XteinkX3Uc8279\n");
  }
  Serial.printf("[detect] active controller=%u\n",
                (unsigned)BoardConfig::ACTIVE.displayController);
  Serial.flush();

  // SPI must be up, with MISO, before the driver owns the display pins.
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  if (isX3) display.setDisplayX3();
  mark(isX3 ? "panel-profile-x3" : "panel-profile-x4");
}

// Time spent drawing, accumulated across a paint's passes so it can be reported
// separately from time spent waiting on the panel. A focus move on Home measured
// far slower than pushing a screen, and both take the same code path, so the
// difference has to be either the drawing (Home draws a dither block and two
// large faces; the placeholder draws almost nothing) or the panel's own waveform
// -- and guessing which would mean optimising blind.
static uint32_t gRenderMs = 0;

// Split the render pass so the log distinguishes drawing from the full-frame
// memcpy into the driver's own buffer, which the reference firmware does not do
// at all -- it draws straight into that buffer through an orientation-aware
// coordinate transform.
//
// There used to be a `rotate` field here too, and it is what this split was
// added to find: a full-frame rotate90CCW cost 37 ms of a 521 ms repaint, paid
// on all 418k pixels whether one changed or every one did, on a text screen that
// inks about 8% of them. It is gone -- the framebuffer now applies the same
// mapping per pixel as it draws -- so the field would report zero forever.
static uint32_t gDrawMs = 0, gCopyMs = 0;

// One render pass, straight into the panel-oriented frame.
//
// The rotation is the framebuffer's, declared once where gFrame is allocated;
// nothing here transposes anything. CCW is the correct direction, verified on X3
// hardware (CW renders the whole screen 180 degrees out -- the two differ by
// exactly half a turn, so swapping them is not the fix for a mirrored image).
// Unverified on X4: if an X4 comes out upside down, the Rotation passed to the
// constructor in setup() is the line, not anything in here.
static void paintPlane(reader::Plane plane) {
  const uint32_t t0 = millis();
  gFrame->clear(true);
  gApp->top().render(*gFrame, *gFonts, gTheme, plane);
  const uint32_t t1 = millis();
  gDrawMs += t1 - t0;
  gRenderMs += t1 - t0;
}

// The 4-level path: base frame, settle pass, two bit-planes, combine, rebase.
// Every comment below was earned by breaking the panel -- LSB before MSB, the
// settle pass before the planes, the Bw re-render instead of an extra frame.
//
// No screen declares Fidelity::Grayscale today, so nothing calls this: chrome
// moved to the one-pass paths, which are ~5x cheaper and legible. It is
// kept because it is the only way to put continuous tone on this panel, which is
// Phase 3's question about book images, and because getting the sequence right
// cost several bricked-looking paints. Do not delete it to remove dead code.
static void paintGray() {
  // 1. The B/W base frame the panel paints first. displayGrayscaleBase() takes
  //    no buffer argument — it drives the driver's own frameBuffer — so
  //    setFramebuffer() (a memcpy) has to land the frame there first.
  paintPlane(reader::Plane::Bw);
  display.setFramebuffer(gFrame->data());
  display.displayGrayscaleBase(EInkDisplay::HALF_REFRESH);
  mark("gray-base-displayed");

  // 2. The settle pass, which leaves the particles receptive to the weak
  //    grayscale nudge waveform. Must run BEFORE the planes are written: the
  //    driver skips it once grayscale planes have overwritten DTM1/DTM2.
  //
  //    DO NOT remove this as a duplicate of the settle inside
  //    displayGrayscaleBase. It was tried: the two issue the same commands --
  //    same XtfPreBwMid bank, same CDI/CCSET/TSSET, same trigger -- so on
  //    inspection it looks like the identical operation done twice, and dropping
  //    it saves 366 ms of a 1363 ms paint. On device the panel then accumulated
  //    ink, everything growing perceptibly thicker with every refresh. The
  //    difference is the controller's state, not the commands: the base's settle
  //    runs having just written the new frame into DTM2, this one runs against
  //    the synced planes, and the particles need both passes before the AA nudge
  //    is safe. Identical command sequences are not identical operations.
  display.preconditionGrayscale();
  mark("gray-preconditioned");

  // 3. The two bit-planes. Both copies go straight out over SPI into controller
  //    RAM and retain no pointer, so the single frame serves both — and served
  //    the base frame above, which the driver has already memcpy'd. LSB must go
  //    first: the MSB copy is dropped unless the driver has seen a valid LSB.
  paintPlane(reader::Plane::Lsb);
  display.copyGrayscaleLsbBuffers(gFrame->data());
  paintPlane(reader::Plane::Msb);
  display.copyGrayscaleMsbBuffers(gFrame->data());
  mark("gray-planes-written");

  // 4. Paint the combined 4-level image (the driver reads the planes it was
  //    handed, not any framebuffer), then put the controller back on a valid
  //    B/W baseline so the next ordinary refresh is differentially sane.
  display.displayGrayBuffer();
  mark("gray-displayed");
  // Re-render the B/W pass rather than keeping a second frame alive for it. The
  //    original reason was that a third frame would not allocate at all (largest
  //    contiguous block ~115 KB against 52 KB frames); drawing straight into
  //    panel orientation has since cut us to one frame, so the headroom is real
  //    now -- but re-rendering is still cheaper than the RAM, and Phase 3's
  //    pagination cache wants that headroom more than this path does.
  paintPlane(reader::Plane::Bw);
  display.cleanupGrayscaleBuffers(gFrame->data());
  mark("refresh-complete");
}

// Hand the one 1-bit frame in gFrame to the panel. Its bytes are already in the
// panel's own landscape orientation, so there is nothing between the render and
// the driver. Shared by both one-pass paths below, which differ only in the
// plane they render.
static void showOnePass(reader::RefreshMode mode) {
  const uint32_t tc = millis();
  display.setFramebuffer(gFrame->data());
  gCopyMs += millis() - tc;
  display.displayBuffer(mode == reader::RefreshMode::Full ? EInkDisplay::FULL_REFRESH
                                                          : EInkDisplay::FAST_REFRESH);
}

// The hard 1-bit path, and what every chrome screen ships on: one render pass
// and one panel waveform against the four-and-three above.
static void paintMono(reader::RefreshMode mode) {
  // Plane::Bw thresholds coverage at half: a pixel is ink or it is paper, and
  // nothing in between survives. That is what the reference firmware does to its
  // chrome -- CrossInk builds its UI fonts 1-bit and reads its anti-aliasing
  // setting only in the reader activities -- and matching it on the same glass is
  // the point. Compared side by side on an X3, the hard edge reads cleaner than a
  // stipple at chrome sizes.
  paintPlane(reader::Plane::Bw);
  showOnePass(mode);
  mark("mono-displayed");
}

// The stippled 1-bit path. Same cost as paintMono -- one render pass, one
// waveform -- and reachable only by a screen that overrides fidelity() to
// Fidelity::Dithered. Nothing does today; chrome ships Mono.
static void paintDithered(reader::RefreshMode mode) {
  // BwDithered, not Bw: this path keeps a soft edge on a two-level frame by
  // stippling glyph and icon edge coverage through a dispersed Bayer threshold
  // rather than thresholding it away. This is the technique freeink-ui.md
  // documents for exactly this ("reproduces the edge coverage on 1-bit panels
  // through its ordered Bayer dither"), and it works because every role in our
  // ramp is 21px or larger -- the doc's guidance is that dithered edges look best
  // from about 16px up. Kept for large display type, where a stroke is wide
  // enough for the stipple to read as tone rather than as grain.
  paintPlane(reader::Plane::BwDithered);
  showOnePass(mode);
  mark("dithered-displayed");
}

static void renderTop() {
  const reader::RefreshMode mode = gRefresh.next(gApp->transition());
  const reader::Fidelity fidelity = gApp->top().fidelity();
  // `mode` is what the POLICY decided, not necessarily what the panel does: a
  // Grayscale screen runs the full three-plane sequence regardless, because that
  // sequence has no differential form. So `fidelity=gray mode=FAST` is not a
  // contradiction -- it means the cadence had a fast slot available and this
  // screen could not use it. Nothing declares Grayscale or Dithered today, so in
  // practice every paint is `fidelity=mono`.
  Serial.printf("[paint] screen=%s fidelity=%s mode=%s sinceFull=%d\n",
                reader::screenName(gApp->top().id()),
                fidelity == reader::Fidelity::Grayscale  ? "gray"
                : fidelity == reader::Fidelity::Dithered ? "dithered"
                                                         : "mono",
                mode == reader::RefreshMode::Full ? "FULL" : "FAST", gRefresh.sinceFull());
  Serial.flush();
  gRenderMs = gDrawMs = gCopyMs = 0;
  const uint32_t t0 = millis();
  // THE OTHER HALF OF THE SHARED-BUS INVARIANT. Every public method of
  // SdFileSystem takes this same recursive guard; this is the one acquisition on
  // the panel side, and it spans the WHOLE paint sequence rather than the SPI
  // writes alone -- the driver keeps the display's CS asserted across its BUSY
  // waits, so a card transfer landing in a wait is still a transfer into an
  // asserted panel. The grayscale path holds it across all four passes and three
  // waveforms for the same reason.
  //
  // Today this is free insurance: paints and card access both run on the Arduino
  // loop task, so they are already serialised by there being one thread, and the
  // only other task (input_task.cpp) touches ADC and GPIO only. It is here to be
  // STRUCTURAL rather than a rule someone has to remember -- the day a background
  // library scan or a cover decode moves off this task, the fault it prevents is
  // intermittent, bus-level and miserable to find.
  SpiBusGuard bus;
  switch (fidelity) {
    case reader::Fidelity::Grayscale:
      // The grayscale sequence is inherently a full repaint; the policy's FAST is
      // not available here.
      paintGray();
      break;
    case reader::Fidelity::Dithered: paintDithered(mode); break;
    case reader::Fidelity::Mono: paintMono(mode); break;
  }
  const uint32_t total = millis() - t0;
  // render = drawing all passes (4 for gray: Bw, Lsb, Msb, then Bw again for the
  // cleanup rebase; 1 for mono and for dithered). panel = everything else, which
  // is essentially BUSY waits. `render` and `draw` are now the same number --
  // drawing is all a render pass does since the rotate went away -- and both are
  // kept so the field stays comparable against the logs that measured the
  // difference. If they ever diverge again, something new got added to the pass.
  Serial.printf("[paint] done total=%lums render=%lums (draw=%lu copy=%lu) panel=%lums\n",
                (unsigned long)total, (unsigned long)gRenderMs, (unsigned long)gDrawMs,
                (unsigned long)gCopyMs, (unsigned long)(total - gRenderMs - gCopyMs));
  Serial.flush();
}

void setup() {
  Serial.begin(115200);
  delay(2500);  // let USB CDC enumerate before the first print
  mark("serial-up");

  detectAndSelectBoard();

  display.begin();
  mark("display-begin-returned");
  // Fresh boot after a flash: force a clean full sync so the panel is not
  // differentially updated against whatever the previous firmware left on it.
  // A fresh boot after a flash must not be differentially updated against
  // whatever the previous firmware left on the panel, so the driver's two
  // initial full clears are right -- they are what flashes the screen black.
  //
  // Waking from deep sleep is a chip reset that looks identical from here, but
  // it is NOT the same situation: e-ink holds its image with no power, so the
  // panel still shows exactly what we painted before sleeping. Clearing then is
  // a black flash to replace a correct image with the same image. Tell the
  // driver the panel is already valid instead.
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  const bool fromSleep = (wake != ESP_SLEEP_WAKEUP_UNDEFINED);
  Serial.printf("[boot] wake cause=%d -> %s\n", (int)wake,
                fromSleep ? "resumed from sleep, panel holds our frame"
                          : "cold boot, clearing the panel");
  Serial.flush();
  if (fromSleep) {
    display.skipInitialResync();
  } else {
    display.requestResync();
  }

  Serial.printf("[info] panel %dx%d, buffer %u bytes\n", display.getDisplayWidth(),
                display.getDisplayHeight(), (unsigned)display.getBufferSize());
  Serial.printf("[info] free heap %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Serial.flush();

  reader::FontSet& fonts = gFonts.emplace();
  // Each role names its weight and FontSet::load checks the asset against it,
  // so a mis-wired pair here is a loud "font-load-FAILED" at boot rather than a
  // screen drawn in the wrong weight for the rest of the project.
  const bool fontsOk =
      fonts.load(reader::Role::Meta400, kFontMeta400, kFontMeta400Size) &&
      fonts.load(reader::Role::Meta500, kFontMeta500, kFontMeta500Size) &&
      fonts.load(reader::Role::Label400, kFontLabel400, kFontLabel400Size) &&
      fonts.load(reader::Role::Label500, kFontLabel500, kFontLabel500Size) &&
      fonts.load(reader::Role::Value500, kFontValue500, kFontValue500Size) &&
      fonts.load(reader::Role::Value700, kFontValue700, kFontValue700Size) &&
      fonts.load(reader::Role::Body400, kFontBody400, kFontBody400Size) &&
      fonts.load(reader::Role::Body500, kFontBody500, kFontBody500Size) &&
      fonts.load(reader::Role::Title700, kFontTitle700, kFontTitle700Size) &&
      fonts.load(reader::Role::Display700, kFontDisplay700, kFontDisplay700Size);
  if (!fontsOk || !fonts.ready()) {
    mark("font-load-FAILED");
    return;
  }
  mark("fonts-ok");

  const int panelW = display.getDisplayWidth();
  const int panelH = display.getDisplayHeight();

  // ONE 1-bit frame. It used to be two -- a portrait one to draw into and a
  // landscape one to rotate the finished frame into -- and the rotate is gone,
  // so the portrait buffer is gone with it: the frame below is drawn against
  // portrait coordinates and stored landscape. That is 52272 bytes of heap given
  // back on the X3 (48000 on the X4), against a measured largest contiguous
  // block of only ~115 KB.
  //
  // Three frames was never possible on this hardware and is worth remembering,
  // because it is the same wall: measured free heap is ~233 KB but the largest
  // contiguous block is ~115 KB, so a third 52 KB allocation found no block big
  // enough even though the total would have covered it. std::vector then throws,
  // and the firmware is built -fno-exceptions, so that is an abort() and a boot
  // loop with no diagnostic. Re-rendering the B/W pass for the grayscale cleanup
  // rebase, rather than retaining a frame for it, is what avoids needing one.
  const unsigned frameBytes = display.getBufferSize();
  const unsigned largest = ESP.getMaxAllocHeap();
  Serial.printf("[info] frame %u bytes x1; free heap %u, largest block %u\n", frameBytes,
                (unsigned)ESP.getFreeHeap(), largest);
  Serial.flush();
  // Fail loudly rather than aborting inside a constructor: a vector that cannot
  // allocate takes the whole firmware down with no diagnostic. One frame is now
  // all that is needed, so this asks for one -- but the check STAYS. It is the
  // only thing standing between a fragmented heap and that silent abort, and the
  // headroom it reports is what will matter when Phase 3 wants a page cache.
  if (largest < frameBytes) {
    Serial.printf("[fatal] largest block %u < one frame (%u)\n", largest, frameBytes);
    mark("frame-alloc-WOULD-FAIL");
    return;
  }
  // Logical portrait, physical landscape: the constructor's dimensions are what
  // the screens draw against (528x792 on the X3), and Rotation::Ccw allocates
  // the store transposed so data() is the panel's own 792x528 buffer. CCW is
  // measured on X3 hardware; see the note on reader::Rotation.
  gFrame = std::make_unique<reader::Framebuffer>(panelH, panelW, reader::Rotation::Ccw);
  mark("frames-allocated");

  // A short buffer would make setFramebuffer's memcpy read past the end, and a
  // zero-length one means the panel geometry came back wrong. This is the check
  // that the rotation is applied to the STORE and not just to the coordinates:
  // a rotated frame whose stride came from the logical width would be exactly
  // this many bytes and still be laid out wrong, so it is not the whole proof --
  // test_rotate.cpp's byte-identity case against rotate90CCW is.
  if (gFrame->sizeBytes() != (int)display.getBufferSize()) {
    Serial.printf("[fatal] frame size %d != driver buffer %u\n", gFrame->sizeBytes(),
                  (unsigned)display.getBufferSize());
    mark("frame-size-MISMATCH");
    gFrame.reset();
    return;
  }

  // Which grayscale path the selected driver actually offers. Logged because
  // the sequence below is only correct for a driver that does NOT combine the
  // base frame into the gray waveform (X3/X4 do not; only Paper Mono does).
  Serial.printf("[info] gray caps: combinesBase=%d busyStaging=%d strip=%d\n",
                display.combinesGrayscaleBase(), display.supportsBusyGrayscaleStaging(),
                display.supportsStripGrayscale());
  Serial.flush();

  // MOUNT THE CARD -- after the display is up, and deliberately so.
  //
  // SPI.begin() ends up called TWICE on one bus: detectAndSelectBoard() calls it
  // with the display's pins, and SDCardManager::begin() calls it again with the
  // card's. That is the reverse of the order the SDK's comments assume, and it is
  // the ordering to be suspicious of if the panel misbehaves after a mount. Two
  // things say it should be benign: begin()'s mitigation is about CS lines, not
  // about who called SPI.begin() last (it drives the display's CS high before
  // probing, because a powered, never-deselected panel breaks card detection),
  // and SdFat issues its own beginTransaction with its own SPISettings on every
  // access, so the bus is reconfigured per transfer either way.
  //
  // Mounting before display.begin() or after it were the two options, and after
  // wins on three counts: the SD-missing screen cannot be painted before the
  // display is up anyway, so a failed mount has nowhere to go; the panel is
  // powered and its CS line settled by the time begin() drives it high to probe
  // the card, which is the condition that mitigation was written for; and the
  // whole ordering question ends up in one identifiable place.
  //
  // ON HARDWARE, THE FIRST PAINT AFTER A MOUNT IS THE THING TO WATCH. A corrupt
  // or hung first refresh with a card in the slot, and a clean one without, is
  // this call order and nothing else.
  mark("sd-mount");
  const bool storage = bringUpStorage("boot");
  // The contract self-test, which is a stub returning -1 unless the firmware was
  // built with -DENCRE_FS_SELFTEST=1 (see sd_selftest.h). Called from here rather
  // than left uncalled so the seam is reachable at all: shell/ has no test
  // harness, and an on-device routine nothing invokes checks nothing.
  const int fsFailures = runSdFsContractSelfTest(gSd);
  if (fsFailures >= 0) {
    Serial.printf("[sd] contract self-test: %d failed assertion(s)\n", fsFailures);
    Serial.flush();
  }

  // Settings on either branch. With no card every FileSystem method fails, so
  // this reports "no file" and applies the compiled-in defaults -- the settings
  // must not depend on the card for the device to behave.
  loadAndApplySettings();

  if (storage) {
    // The root is Home, built from the shared catalogue. Nothing rebuilds it, so
    // popping back to Home returns this object with its focus intact.
    buildHomeApp();
    mark("root-home");
  } else {
    // A missing card is a DESIGNED SCREEN (spec 6: "device never boots into a
    // broken UI"), not a hang and not a Home screen with no books. It is the
    // root, not a screen pushed over Home: there is nothing behind it to go back
    // to, which is why its Back slot is empty.
    gApp = std::make_unique<reader::App>(std::make_unique<reader::SdMissingScreen>(), gFactory);
    mark("root-sd-missing");
  }

  // WHERE THE USER WAS. Only across a genuine wake: a device that boots into a
  // sub-screen after a week off is confusing, and 2B already distinguishes the
  // two cases from esp_sleep_get_wakeup_cause().
  if (!fromSleep) {
    // Cold boot starts at Home and forgets the record, so the next wake cannot
    // resume a screen from a previous run of the device.
    clearSession();
  } else if (storage) {
    Session s;
    if (loadSession(s) && s.screen != reader::ScreenId::Home) {
      if (s.screen == reader::ScreenId::SdMissing) {
        // The mount above already decided this, and it decided there IS a card.
        // Restoring the no-card screen over a working card would be showing the
        // user a message that is no longer true.
        Serial.printf("[session] the record says SD-MISSING but the card mounted; Home\n");
        Serial.flush();
      } else if (gApp->pushScreen(s.screen)) {
        // Home stays underneath, so Back works. Only ONE screen is restored, so a
        // record naming a screen that was two deep (Settings > Input Monitor)
        // comes back with Home under it rather than Settings -- the record holds
        // one id, not a path. Nothing in V1 is unreachable that way; a restored
        // path is 2C-2's if the deeper screens make it worth one.
        mark("session-restored");
      } else {
        // The factory refused it: an id this build has no case for, from a newer
        // firmware's record. Home is already the root, so there is nothing to
        // undo.
        Serial.printf("[session] cannot build screen=%s; staying on Home\n",
                      reader::screenName(s.screen));
        Serial.flush();
      }
    }
  }
  // ...and if we woke with no card, the record is left alone rather than cleared:
  // it is still true, and the next wake with a card in the slot can honour it.

  // Before the first poll, not just after each dispatch: a hold started on the
  // very first frame must be recognised too.
  gPresses.setLongPressable(gApp->longPressable());
  gInput.begin();
  startInputTask(gInput);
  mark("input-started");

  renderTop();
  // The paint above satisfied the App's initial dirty flag. Without this the
  // first loop() iteration would repaint an identical Home and spend another
  // 1.5 s of panel time on it.
  gApp->clearDirty();

  // The driver grants itself TWO full clears at init (_initialFullsRemaining),
  // tuned for a consumer that paints a splash before its first real screen. We
  // paint the real screen immediately, so the paint above already cleared the
  // panel and the second clear lands on the user's FIRST BUTTON PRESS -- a
  // black flash and an extra 693 ms DRF on an otherwise ordinary focus move,
  // which is exactly why it read as random. Measured: 2113 ms, 2056 ms, then
  // 1363 ms for every paint after.
  //
  // Spend the rest of the budget here. The panel now holds a frame we just
  // wrote, which is the assertion skipInitialResync exists to make.
  display.skipInitialResync();
  mark("first-paint-complete");
}

[[noreturn]] static void sleepNow() {
  // The Sleep screen is boarded and belongs to Phase 2C. Painting nothing is
  // not a gap in the picture: e-ink holds its last image with no power, so the
  // device keeps showing whatever you were looking at.
  Serial.printf("[power] sleeping; wake with the power button\n");
  Serial.flush();
  display.deepSleep();
  // Cuts the X3's SD rail (GPIO13) and any other gated rail, latched so the
  // switches stay off through sleep. Without it the card stays powered and
  // drains the battery all night.
  freeink::PowerManager::powerDownRailsForSleep();
  // Waits for release, arms the SoC-correct wake source from the board's power
  // pin and polarity, then sleeps. Wake is a chip RESET, so this never returns:
  // setup() runs again, sees a wake cause, and restores the screen from the NVS
  // session record -- which is why nothing is saved here. The record is written
  // after every dispatch, so it is already current, and Power is handled BEFORE
  // dispatch (a power press changes no screen), so there is nothing left to
  // store at this point.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void loop() {
  // setup() bails out without building the app on a font-load, heap or geometry
  // failure. Repeat the last stage reached so the hang point is visible even
  // when the host attaches late; dereferencing a null gApp below would turn a
  // diagnosable failure into a crash loop that looks like a bootloader hang.
  if (!gApp) {
    static uint32_t n = 0;
    Serial.printf("[alive] %lu last-stage=%s heap=%u (setup did not complete)\n",
                  (unsigned long)++n, stage, (unsigned)ESP.getFreeHeap());
    Serial.flush();
    delay(2000);
    return;
  }

  bool activity = false;
  RawSample s{};
  while (popRawSample(s)) {
    activity = true;
    // Explicit, not a cast. The SDK's BTN_* values happen to match Button's
    // order today, and a silent reinterpret would break the day either changes.
    reader::Button b;
    switch (s.button) {
      // The SDK's names describe ITS band order, not this device's front panel,
      // and on the Xteink they do not agree. Measured on an X3 with the input
      // monitor, pressing each button in turn:
      //
      //   front row, left to right : BTN_BACK  BTN_CONFIRM  BTN_LEFT  BTN_RIGHT
      //   the two side buttons     : BTN_UP    BTN_DOWN
      //
      // The design puts focus navigation on the front-right pair, under their
      // own hint slots, and page turns on the sides with no hints (spec 4.0).
      // So the front-right pair is Up/Down and the SIDE pair is Left/Right --
      // the two names the SDK gives them, swapped. Mapping straight through is
      // what put focus on the side buttons and left the front pair dead.
      case InputManager::BTN_BACK: b = reader::Button::Back; break;
      case InputManager::BTN_CONFIRM: b = reader::Button::Confirm; break;
      case InputManager::BTN_LEFT: b = reader::Button::Up; break;
      case InputManager::BTN_RIGHT: b = reader::Button::Down; break;
      // The sides turn pages in the Reader (Phase 3) and do nothing before it,
      // so which one is "left" is unverified -- there is no behaviour to check
      // it against yet. Verify when page turns land.
      case InputManager::BTN_UP: b = reader::Button::Left; break;
      case InputManager::BTN_DOWN: b = reader::Button::Right; break;
      case InputManager::BTN_POWER: b = reader::Button::Power; break;
      default: continue;
    }
    gPresses.sample(b, s.down, s.ms);
  }
  // Every poll, with or without a transition: a hold fires while the button is
  // still down, so without this a long press never resolves at all.
  gPresses.tick(millis());
  if (activity) {
    gIdle.noteActivity(millis());
    gLastInputMs = millis();
  }

  reader::InputEvent ev{};
  while (gPresses.pop(ev)) {
    Serial.printf("[input] %s %s\n", reader::buttonName(ev.button),
                  ev.kind == reader::PressKind::Long ? "LONG" : "SHORT");
    if (ev.button == reader::Button::Power) sleepNow();
    gApp->dispatch(ev);
    // Between the dispatch and the mask refresh below, so the refresh sees
    // whatever screen the retry left on top -- on success that is a brand new App
    // rooted at Home, whose holds are not the SD-missing screen's.
    if (gApp->retryRequested()) handleRetry();
    // The mask belongs to whatever screen is now on top, which a push or pop
    // just changed. Re-reading it here is what keeps a hold bound only where a
    // ring is drawn.
    gPresses.setLongPressable(gApp->longPressable());
    // Where the user is now, for a wake to restore. An unchanged record is not
    // rewritten, so this is nearly free on an event that did not move the stack.
    saveWhereWeAre();
  }

  if (gApp->sleepRequested() || gIdle.tick(millis()) == reader::PowerAction::Sleep) sleepNow();

  // Coalesce a burst of presses into one paint.
  //
  // A paint costs the panel ~1.3 s and cannot be interrupted, so holding Down
  // through a three-item menu used to cost three of them -- 4 s to show two
  // intermediate focus states nobody wanted to see, with every press landing
  // further behind. Waiting for input to go quiet first means a burst paints
  // once, at its final state.
  //
  // The cost is kCoalesceMs added to a single isolated press. That is a ~7%
  // penalty on one paint against a ~3x saving on a burst, and it is below what
  // is noticeable next to the refresh itself.
  const bool settled = static_cast<uint32_t>(millis() - gLastInputMs) >= kCoalesceMs;
  if (gApp->dirty() && settled) {
    renderTop();
    gApp->clearDirty();
  }

  static uint32_t beat = 0;
  if (++beat % 200 == 0) {
    Serial.printf("[alive] last-stage=%s heap=%u screen=%s depth=%d dropped=%lu/%lu\n", stage,
                  (unsigned)ESP.getFreeHeap(), reader::screenName(gApp->top().id()),
                  gApp->depth(), (unsigned long)rawSamplesDropped(),
                  (unsigned long)gPresses.dropped());
    Serial.flush();
  }
  delay(10);
}
