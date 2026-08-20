#include <Arduino.h>
#include <EInkDisplay.h>

#include "font_spacegrotesk_16.h"
#include "reader/framebuffer.h"
#include "reader/rotate.h"
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"

// Xteink display SPI pins (same wiring as CrossPoint's HalGPIO).
constexpr int8_t EPD_SCLK = 8, EPD_MOSI = 10, EPD_CS = 21, EPD_DC = 4, EPD_RST = 5, EPD_BUSY = 6;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

void setup() {
  Serial.begin(115200);
  // X4 geometry by default; X3 runtime detection lands in Phase 2
  // (call display.setDisplayX3() before begin() for an X3 unit).
  display.begin();

  reader::QuietTheme theme;
  if (!theme.loadFonts(kUiFont, kUiFontSize)) {
    Serial.println("font load failed");
    return;
  }

  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 \xE2\x80\x94 MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};

  const int panelW = display.getDisplayWidth();    // 800 on X4
  const int panelH = display.getDisplayHeight();   // 480 on X4
  reader::Framebuffer portrait(panelH, panelW);    // 480 x 800
  reader::Framebuffer landscape(panelW, panelH);   // 800 x 480
  theme.renderHome(portrait, vm);
  reader::rotate90CW(portrait, landscape);

  display.setFramebuffer(landscape.data());
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.println("home rendered");
}

void loop() { delay(1000); }
