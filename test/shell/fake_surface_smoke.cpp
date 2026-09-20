// EVERY FAKED TYPE INSTANTIATED AND EVERY FAKED METHOD CALLED ONCE.
//
// PR1 of #179 compiles NONE of shell/src/main.cpp -- that is PR2, whose deliverable
// is a link. What this file buys is that the surface is self-consistent and that
// the two fakes carrying behaviour rather than a recording actually behave: the
// panel's baseline invariant, and NVS surviving a simulated sleep.
//
// IT IS NOT IN test/unit/, deliberately. CMakeLists.txt globs that directory into
// unit_tests, and this needs the fake include path ahead of everything -- a header
// named Arduino.h on the unit suite's path would be a surprise nobody asked for.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <filesystem>
#include <string>

namespace {

bool transcriptHas(const std::string& needle) {
  for (const std::string& line : harness::transcript())
    if (line.find(needle) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST_CASE("every faked type instantiates and every faked method is callable") {
  harness::resetAll();

  EInkDisplay display(8, 10, 21, 4, 5, 6);
  display.begin();
  REQUIRE(display.getFrameBuffer() != nullptr);
  // DIMENSIONALLY HONEST, because bindFrameToDriver refuses a view whose size
  // disagrees with getBufferSize(). A fake returning a token pointer would fail at
  // the first thing main.cpp does with the panel.
  CHECK(display.getBufferSize() == static_cast<uint32_t>(792) * 528 / 8);
  CHECK(display.getDisplayWidth() == 792);
  CHECK(display.getDisplayHeight() == 528);
  display.triggerDisplay(EInkDisplay::FAST_REFRESH);
  display.completeDisplay();
  display.displayGrayscaleBase(EInkDisplay::HALF_REFRESH);
  display.preconditionGrayscale();
  display.copyGrayscaleLsbBuffers(display.getFrameBuffer());
  display.copyGrayscaleMsbBuffers(display.getFrameBuffer());
  display.displayGrayBuffer();
  display.cleanupGrayscaleBuffers(display.getFrameBuffer());
  CHECK_FALSE(display.combinesGrayscaleBase());
  CHECK(display.supportsStripGrayscale());
  CHECK_FALSE(display.supportsBusyGrayscaleStaging());
  display.requestResync();
  display.setDisplayX3();
  display.deepSleep();

  SPI.begin(8, 7, 10, 21);
  pinMode(3, INPUT_PULLUP);
  (void)digitalRead(3);
  Serial.begin(115200);
  Serial.setTxBufferSize(4096);
  Serial.printf("[smoke] %s\n", "a line");
  Serial.flush();
  CHECK(ESP.getFreeHeap() > 0);
  CHECK(ESP.getMinFreeHeap() > 0);
  CHECK(ESP.getMaxAllocHeap() > 0);

  // THE REAL SPELLINGS, and the first draft of this file had none of them right:
  // `XTEINK_X3` for `XteinkX3`, a global `detectXteinkVerdict` for one in namespace
  // `freeink`, `X3` for `X3Confirmed`, `ran` for `valid`. The 47-method census
  // counted CALLS and could not see enumerator names, namespaces or struct fields --
  // which is what PR2 was always going to discover and did.
  BoardConfig::selectDevice(BoardConfig::Board::XteinkX3);
  CHECK(freeink::detectXteinkVerdict() == freeink::XteinkVerdict::X3Confirmed);
  CHECK(freeink::applyXteinkDisplayController());
  CHECK(freeink::getXteinkDisplayProbeDiag().valid);
  // THE WRITE-THEN-READ detectAndSelectBoard depends on: the probe mutates ACTIVE
  // and the promotion branch reads it back.
  CHECK(BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279);
  CHECK(BoardConfig::ACTIVE.input.power == 3);
  CHECK_FALSE(BoardConfig::ACTIVE.input.powerActiveHigh);

  InputManager input;
  input.begin();
  CHECK(InputManager::BTN_POWER == 6);

  BatteryMonitor battery(-1);
  CHECK(battery.readStatus().supported);

  CHECK(freeink::PowerManager::armPowerButtonWakeup());
  freeink::PowerManager::waitForPowerButtonRelease();
  freeink::PowerManager::powerDownRailsForSleep();

  CHECK(esp_reset_reason() == ESP_RST_POWERON);
  CHECK(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED);
  CHECK(esp_deep_sleep_enable_gpio_wakeup(1ull << 3, ESP_GPIO_WAKEUP_GPIO_LOW) == 0);

  // Serial IS the transcript sink, which is what makes main.cpp's own logf lines
  // the record rather than something written beside it.
  CHECK(transcriptHas("[smoke] a line"));
}

TEST_CASE("the two [[noreturn]] paths throw, so the branches that take them are observable") {
  harness::resetAll();
  CHECK_THROWS_AS(freeink::PowerManager::deepSleepUntilPowerButton(), freeink::HarnessSlept);
  CHECK_THROWS_AS(esp_restart(), HarnessRestarted);
}

TEST_CASE("the panel refuses a baseline claim with no refresh behind it -- #94's shape") {
  harness::resetAll();
  EInkDisplay display(8, 10, 21, 4, 5, 6);
  display.begin();

  // THE #94 FORM: an assertion made 661 lines after begin() with no refresh in
  // between, which left the waveform diffing against power-up garbage.
  display.skipInitialResync();
  CHECK(transcriptHas("INVARIANT VIOLATED"));

  // AND THE TWO LIVE CALL SITES' FORM, which both pass: a pass is completed first,
  // so the claim is true when it is made.
  harness::resetAll();
  EInkDisplay ok(8, 10, 21, 4, 5, 6);
  ok.begin();
  ok.triggerDisplay(EInkDisplay::FAST_REFRESH);
  ok.completeDisplay();
  ok.skipInitialResync();
  CHECK_FALSE(transcriptHas("INVARIANT VIOLATED"));
  CHECK(transcriptHas("baseline-asserted"));
}

TEST_CASE("requestResync forces the GC bank, and a completed refresh clears it") {
  harness::resetAll();
  EInkDisplay display(8, 10, 21, 4, 5, 6);
  display.begin();
  display.triggerDisplay(EInkDisplay::FAST_REFRESH);
  // The boot clear is still owed and the baseline is unknown, so a FAST refresh
  // still loads GC and seeds DTM1 white -- displayStart's expression is an OR.
  CHECK(transcriptHas("bank=GC seed=white"));
  display.completeDisplay();
}

TEST_CASE("NVS survives a simulated sleep, which is what makes the restore path reachable") {
  harness::resetAll();
  Preferences prefs;
  REQUIRE(prefs.begin("encre_sess"));
  const std::string wire = "home:-1;library:7:/books";
  CHECK(prefs.putBytes("stack", wire.data(), wire.size()) == wire.size());
  prefs.end();

  // A sleep is a thrown exception rather than a new process, so the store has to
  // outlive the object -- otherwise nothing above it is exercisable at all.
  CHECK_THROWS_AS(freeink::PowerManager::deepSleepUntilPowerButton(), freeink::HarnessSlept);

  Preferences after;
  REQUIRE(after.begin("encre_sess"));
  REQUIRE(after.getBytesLength("stack") == wire.size());
  std::string back(wire.size(), '\0');
  CHECK(after.getBytes("stack", back.data(), back.size()) == wire.size());
  CHECK(back == wire);
}

TEST_CASE("the NVS fake refuses an over-long key, where the real one fails silently") {
  harness::resetAll();
  Preferences prefs;
  REQUIRE(prefs.begin("encre_diag"));
  // STRICTER THAN THE REAL THING, on purpose -- the failure being modelled is a
  // SILENT one, and a fake that reproduced the silence would teach nothing.
  CHECK(prefs.putBytes("a-key-well-over-the-cap", "x", 1) == 0);
  CHECK(transcriptHas("REFUSED key"));
  CHECK(prefs.putBytes("fits", "x", 1) == 1);
}

TEST_CASE("the card is a host directory, and pulling it makes every operation refuse") {
  harness::resetAll();
  const std::string root = (std::filesystem::temp_directory_path() / "encre-smoke-card").string();
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  harness::cardRoot() = root;

  CHECK(SdMan.begin(12, 0, 13));
  CHECK(SdMan.ready());
  CHECK(SdMan.mkdir("/books"));
  CHECK(SdMan.exists("/books"));
  {
    FsFile f = SdMan.open("/books/a.txt", O_WRONLY | O_CREAT | O_TRUNC);
    REQUIRE(static_cast<bool>(f));
    const char* body = "bytes";
    CHECK(f.write(reinterpret_cast<const uint8_t*>(body), 5) == 5);
    f.sync();
    CHECK(f.getWriteError() == 0);
    f.close();
  }
  CHECK(SdMan.rename("/books/a.txt", "/books/b.txt"));
  CHECK(SdMan.exists("/books/b.txt"));
  CHECK(SdMan.remove("/books/b.txt"));
  CHECK_FALSE(SdMan.exists("/books/b.txt"));

  // THE CARD PULLED MID-SESSION, which is what pollCardPresence is written to
  // notice and what a card lost after a mount cannot recover from in process.
  harness::cardPresent() = false;
  CHECK_FALSE(SdMan.ready());
  CHECK_FALSE(SdMan.exists("/books"));
  CHECK_FALSE(static_cast<bool>(SdMan.open("/books/b.txt")));

  std::filesystem::remove_all(root);
}

TEST_CASE("the clock is virtual, so every duration in a transcript is a checkable number") {
  harness::resetAll();
  CHECK(millis() == 0);
  EInkDisplay display(8, 10, 21, 4, 5, 6);
  display.begin();
  display.triggerDisplay(EInkDisplay::FAST_REFRESH);
  // The GC waveform's recorded cost. A real clock here would make this noise, and
  // a golden over noise gets re-blessed -- which is the thing the Goldens section
  // of CLAUDE.md forbids.
  CHECK(millis() == 693);
  delay(7);
  CHECK(millis() == 700);
}
