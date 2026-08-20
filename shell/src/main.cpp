#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <SPI.h>
#include <XteinkDetect.h>

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
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"
#include "reader/text.h"  // reader::Plane
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

// Xteink display SPI pins. Shared by X3 and X4; MISO is shared with the SD card.
constexpr int8_t EPD_SCLK = 8, EPD_MOSI = 10, EPD_CS = 21, EPD_DC = 4, EPD_RST = 5,
                 EPD_BUSY = 6, SPI_MISO = 7;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

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

void setup() {
  Serial.begin(115200);
  delay(2500);  // let USB CDC enumerate before the first print
  mark("serial-up");

  detectAndSelectBoard();

  display.begin();
  mark("display-begin-returned");
  // Fresh boot after a flash: force a clean full sync so the panel is not
  // differentially updated against whatever the previous firmware left on it.
  display.requestResync();

  Serial.printf("[info] panel %dx%d, buffer %u bytes\n", display.getDisplayWidth(),
                display.getDisplayHeight(), (unsigned)display.getBufferSize());
  Serial.printf("[info] free heap %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Serial.flush();

  reader::FontSet fonts;
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
  reader::QuietTheme theme;
  mark("fonts-ok");

  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 \xE2\x80\x94 MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

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
  reader::Framebuffer portrait(panelH, panelW);
  reader::Framebuffer landscape(panelW, panelH);
  mark("frames-allocated");

  // A short buffer would make setFramebuffer's memcpy read past the end, and a
  // zero-length one means the panel geometry came back wrong.
  if (landscape.sizeBytes() != (int)display.getBufferSize() ||
      portrait.sizeBytes() != (int)display.getBufferSize()) {
    Serial.printf("[fatal] frame size %d/%d != driver buffer %u\n", portrait.sizeBytes(),
                  landscape.sizeBytes(), (unsigned)display.getBufferSize());
    mark("frame-size-MISMATCH");
    return;
  }

  // Which grayscale path the selected driver actually offers. Logged because
  // the sequence below is only correct for a driver that does NOT combine the
  // base frame into the gray waveform (X3/X4 do not; only Paper Mono does).
  Serial.printf("[info] gray caps: combinesBase=%d busyStaging=%d strip=%d\n",
                display.combinesGrayscaleBase(), display.supportsBusyGrayscaleStaging(),
                display.supportsStripGrayscale());
  Serial.flush();

  // One render pass: draw the plane portrait-side, then rotate into `out`.
  // CCW is the correct direction, verified on X3 hardware: CW renders the whole
  // screen 180 degrees out (the two directions differ by exactly half a turn).
  // Unverified on X4 — if an X4 comes out upside down, this is the line.
  auto paint = [&](reader::Plane plane, reader::Framebuffer& out) {
    portrait.clear(true);
    theme.renderHome(portrait, fonts, vm, plane);
    reader::rotate90CCW(portrait, out);
  };

  // 1. The B/W base frame the panel paints first. displayGrayscaleBase() takes
  //    no buffer argument — it drives the driver's own frameBuffer — so
  //    setFramebuffer() (a memcpy) has to land the frame there first.
  paint(reader::Plane::Bw, landscape);
  display.setFramebuffer(landscape.data());
  display.displayGrayscaleBase(EInkDisplay::HALF_REFRESH);
  mark("gray-base-displayed");

  // 2. The X3 settle pass, which leaves the particles receptive to the weak
  //    grayscale nudge waveform. Must run BEFORE the planes are written: the
  //    driver skips it once grayscale planes have overwritten DTM1/DTM2.
  display.preconditionGrayscale();
  mark("gray-preconditioned");

  // 3. The two bit-planes. Both copies go straight out over SPI into controller
  //    RAM and retain no pointer, so one landscape buffer serves both — and the
  //    base frame above, which the driver has already memcpy'd. LSB must go
  //    first: the MSB copy is dropped unless the driver has seen a valid LSB.
  paint(reader::Plane::Lsb, landscape);
  display.copyGrayscaleLsbBuffers(landscape.data());
  paint(reader::Plane::Msb, landscape);
  display.copyGrayscaleMsbBuffers(landscape.data());
  mark("gray-planes-written");

  // 4. Paint the combined 4-level image (the driver reads the planes it was
  //    handed, not any framebuffer), then put the controller back on a valid
  //    B/W baseline so the next ordinary refresh is differentially sane.
  display.displayGrayBuffer();
  mark("gray-displayed");
  // Re-render the B/W pass rather than having kept a third frame alive for it.
  paint(reader::Plane::Bw, landscape);
  display.cleanupGrayscaleBuffers(landscape.data());
  mark("refresh-complete");
}

void loop() {
  // Repeat the last stage reached so the hang point is visible even when the
  // host attaches late. Silence here means setup() never returned.
  static uint32_t n = 0;
  Serial.printf("[alive] %lu last-stage=%s heap=%u\n", (unsigned long)++n, stage,
                (unsigned)ESP.getFreeHeap());
  Serial.flush();
  delay(2000);
}
