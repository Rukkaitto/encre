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
#include "reader/power.h"
#include "reader/refresh.h"
#include "reader/rotate.h"
#include "reader/screen_home.h"
#include "reader/screens.h"
#include "reader/text.h"  // reader::Plane
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

// Xteink display SPI pins. Shared by X3 and X4; MISO is shared with the SD card.
constexpr int8_t EPD_SCLK = 8, EPD_MOSI = 10, EPD_CS = 21, EPD_DC = 4, EPD_RST = 5,
                 EPD_BUSY = 6, SPI_MISO = 7;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

// Sleep after five minutes idle, FULL refresh every fifteen. Both become
// settings in Phase 2C; named here so the numbers are not buried in a
// constructor call.
constexpr uint32_t kSleepAfterMs = 5u * 60u * 1000u;
constexpr int kFullRefreshEvery = 15;
// How long input must be quiet before a repaint starts, so a burst of presses
// costs one paint instead of one each. See the coalescing comment in loop().
constexpr uint32_t kCoalesceMs = 90;

// EXPERIMENT: force every screen onto the 1-bit path regardless of the fidelity
// it declares.
//
// The grayscale path is structurally ~5x slower than 1-bit and always will be:
// three panel waits (366 + 366 + 156 ms) and four render passes, against one
// wait and one pass. CrossInk feels faster because it does not use it -- the
// SDK's own UC8279 comment notes "CrossPoint paints home with FAST".
//
// 1-bit chrome was rejected once, in Phase 2A-2, as illegible. But that was at
// the OLD type ramp, which was authored on a monitor and measured roughly half a
// legible size on this glass; the pt-at-150-DPI ramp landed afterwards and 1-bit
// has never been looked at since. Anti-aliasing was kept as a preference, not as
// the legibility fix. So this is worth an honest look before building partial
// updates on top of the grayscale path.
//
// Set false to go back to anti-aliased chrome.
constexpr bool kForceMonoChrome = true;

// millis() of the last button transition, for the coalescing window. Starts at 0
// so the first paint in setup() is never deferred.
static uint32_t gLastInputMs = 0;

// Everything the render needs has to outlive setup(), so it lives here rather
// than on setup()'s stack -- but the two frames stay heap-allocated behind
// unique_ptr on purpose. A file-scope Framebuffer would allocate during static
// init, before the largest-block check in setup() could run, and a vector that
// cannot allocate under -fno-exceptions is an abort() boot loop with no
// diagnostic. This project has already lost a boot to exactly that.
static std::unique_ptr<reader::Framebuffer> gPortrait, gLandscape;
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
static reader::RefreshPolicy gRefresh(kFullRefreshEvery);
static reader::IdleTimer gIdle(kSleepAfterMs);
static InputManager gInput;

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

// One render pass: draw the plane portrait-side, then rotate into `gLandscape`.
// CCW is the correct direction, verified on X3 hardware: CW renders the whole
// screen 180 degrees out (the two directions differ by exactly half a turn).
// Unverified on X4 — if an X4 comes out upside down, this is the line.
static void paintPlane(reader::Plane plane) {
  const uint32_t t0 = millis();
  gPortrait->clear(true);
  gApp->top().render(*gPortrait, *gFonts, gTheme, plane);
  reader::rotate90CCW(*gPortrait, *gLandscape);
  gRenderMs += millis() - t0;
}

// The 4-level path: base frame, settle pass, two bit-planes, combine, rebase.
// Every comment below was earned by breaking the panel -- LSB before MSB, the
// settle pass before the planes, the Bw re-render instead of a third frame.
static void paintGray() {
  // 1. The B/W base frame the panel paints first. displayGrayscaleBase() takes
  //    no buffer argument — it drives the driver's own frameBuffer — so
  //    setFramebuffer() (a memcpy) has to land the frame there first.
  paintPlane(reader::Plane::Bw);
  display.setFramebuffer(gLandscape->data());
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
  //    RAM and retain no pointer, so one landscape buffer serves both — and the
  //    base frame above, which the driver has already memcpy'd. LSB must go
  //    first: the MSB copy is dropped unless the driver has seen a valid LSB.
  paintPlane(reader::Plane::Lsb);
  display.copyGrayscaleLsbBuffers(gLandscape->data());
  paintPlane(reader::Plane::Msb);
  display.copyGrayscaleMsbBuffers(gLandscape->data());
  mark("gray-planes-written");

  // 4. Paint the combined 4-level image (the driver reads the planes it was
  //    handed, not any framebuffer), then put the controller back on a valid
  //    B/W baseline so the next ordinary refresh is differentially sane.
  display.displayGrayBuffer();
  mark("gray-displayed");
  // Re-render the B/W pass rather than having kept a third frame alive for it.
  paintPlane(reader::Plane::Bw);
  display.cleanupGrayscaleBuffers(gLandscape->data());
  mark("refresh-complete");
}

// The 1-bit path. Only legitimate where thresholded text is still readable --
// the Reader's body text (Phase 3) and diagnostics. Phase 2A-2 measured
// thresholded CHROME as illegible, so no product chrome screen may use this.
static void paintMono(reader::RefreshMode mode) {
  paintPlane(reader::Plane::Bw);
  display.setFramebuffer(gLandscape->data());
  display.displayBuffer(mode == reader::RefreshMode::Full ? EInkDisplay::FULL_REFRESH
                                                          : EInkDisplay::FAST_REFRESH);
  mark("mono-displayed");
}

static void renderTop() {
  const reader::RefreshMode mode = gRefresh.next(gApp->transition());
  const bool gray = !kForceMonoChrome && gApp->top().fidelity() == reader::Fidelity::Gray;
  // `mode` is what the POLICY decided, not necessarily what the panel does: a
  // Gray screen runs the full three-plane sequence regardless, because a 1-bit
  // fast refresh of chrome is illegible on this glass. So `fidelity=gray
  // mode=FAST` is not a contradiction -- it means the cadence had a fast slot
  // available and this screen could not use it.
  Serial.printf("[paint] screen=%s fidelity=%s mode=%s sinceFull=%d\n",
                reader::screenName(gApp->top().id()), gray ? "gray" : "mono",
                mode == reader::RefreshMode::Full ? "FULL" : "FAST", gRefresh.sinceFull());
  Serial.flush();
  gRenderMs = 0;
  const uint32_t t0 = millis();
  if (gray) {
    // The grayscale sequence is inherently a full repaint; the policy's FAST is
    // not available here, and taking it would mean thresholded chrome.
    paintGray();
  } else {
    paintMono(mode);
  }
  const uint32_t total = millis() - t0;
  // render = drawing all passes (4 for gray: Bw, Lsb, Msb, then Bw again for the
  // cleanup rebase; 1 for mono). panel = everything else, which is essentially
  // BUSY waits.
  Serial.printf("[paint] done total=%lums render=%lums panel=%lums\n", (unsigned long)total,
                (unsigned long)gRenderMs, (unsigned long)(total - gRenderMs));
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

  // TWO 1-bit frames, not three. Three (portrait + gray scratch + a retained
  // B/W base for the cleanup rebase) aborts on this hardware: measured free
  // heap is ~233 KB but the largest contiguous block is only ~115 KB, so the
  // third 52 KB allocation finds no block big enough even though the total
  // would cover it. std::vector then throws, and the firmware is built
  // -fno-exceptions, so that is an abort() and a boot loop. Re-rendering the
  // B/W pass for the cleanup rebase costs one extra render and saves a frame.
  const unsigned frameBytes = display.getBufferSize();
  const unsigned largest = ESP.getMaxAllocHeap();
  Serial.printf("[info] frame %u bytes x2; free heap %u, largest block %u\n", frameBytes,
                (unsigned)ESP.getFreeHeap(), largest);
  Serial.flush();
  // Fail loudly rather than aborting inside a constructor: a vector that cannot
  // allocate takes the whole firmware down with no diagnostic.
  if (largest < frameBytes * 2) {
    Serial.printf("[fatal] largest block %u < two frames (%u)\n", largest, frameBytes * 2);
    mark("frame-alloc-WOULD-FAIL");
    return;
  }
  gPortrait = std::make_unique<reader::Framebuffer>(panelH, panelW);
  gLandscape = std::make_unique<reader::Framebuffer>(panelW, panelH);
  mark("frames-allocated");

  // A short buffer would make setFramebuffer's memcpy read past the end, and a
  // zero-length one means the panel geometry came back wrong.
  if (gLandscape->sizeBytes() != (int)display.getBufferSize() ||
      gPortrait->sizeBytes() != (int)display.getBufferSize()) {
    Serial.printf("[fatal] frame size %d/%d != driver buffer %u\n", gPortrait->sizeBytes(),
                  gLandscape->sizeBytes(), (unsigned)display.getBufferSize());
    mark("frame-size-MISMATCH");
    gPortrait.reset();
    gLandscape.reset();
    return;
  }

  // Which grayscale path the selected driver actually offers. Logged because
  // the sequence below is only correct for a driver that does NOT combine the
  // base frame into the gray waveform (X3/X4 do not; only Paper Mono does).
  Serial.printf("[info] gray caps: combinesBase=%d busyStaging=%d strip=%d\n",
                display.combinesGrayscaleBase(), display.supportsBusyGrayscaleStaging(),
                display.supportsStripGrayscale());
  Serial.flush();

  // The root is Home, built from the shared catalogue. Nothing rebuilds it, so
  // popping back to Home returns this object with its focus intact.
  gApp = std::make_unique<reader::App>(
      std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
      gFactory);
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
  // pin and polarity, then sleeps. Wake is a chip RESET, so this never returns
  // and the firmware boots into Home -- restoring the last screen needs the
  // settings store, which is Phase 2C.
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
    // The mask belongs to whatever screen is now on top, which a push or pop
    // just changed. Re-reading it here is what keeps a hold bound only where a
    // ring is drawn.
    gPresses.setLongPressable(gApp->longPressable());
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
