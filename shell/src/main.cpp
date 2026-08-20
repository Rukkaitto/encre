#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <SPI.h>
#include <XteinkDetect.h>

#include "font_body.h"
#include "font_label.h"
#include "font_meta.h"
#include "font_title.h"
#include "font_value.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"
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
  const bool fontsOk = fonts.load(reader::Role::Meta, kFontMeta, kFontMetaSize) &&
                       fonts.load(reader::Role::Label, kFontLabel, kFontLabelSize) &&
                       fonts.load(reader::Role::Value, kFontValue, kFontValueSize) &&
                       fonts.load(reader::Role::Body, kFontBody, kFontBodySize) &&
                       fonts.load(reader::Role::Title, kFontTitle, kFontTitleSize);
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
  reader::Framebuffer portrait(panelH, panelW);
  mark("portrait-allocated");
  theme.renderHome(portrait, fonts, vm);
  mark("rendered-to-framebuffer");

  // The landscape buffer is still constructed only after renderHome returns.
  // Ink::White made the old full-screen scratch buffer redundant, so the render
  // no longer spikes the heap, but two full framebuffers is 96 KB on a 320 KB
  // part and there is no reason to hold both live any longer than the rotate.
  reader::Framebuffer landscape(panelW, panelH);
  // CCW is the correct direction, verified on X3 hardware: CW renders the whole
  // screen 180 degrees out (the two directions differ by exactly half a turn).
  // Unverified on X4 — if an X4 comes out upside down, this is the line.
  reader::rotate90CCW(portrait, landscape);
  mark("rotated");

  display.setFramebuffer(landscape.data());
  mark("framebuffer-handed-to-driver");
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
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
