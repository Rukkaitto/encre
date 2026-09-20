#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include "harness_state.h"

namespace freeink {

// THE PANEL, RECORDED AND MODELLED.
//
// A pure recorder cannot catch #94, which is the defect this fake exists for: the
// bug was a CLAIM about the controller's DTM1 baseline, and whether that claim was
// false depends on state no transcript of calls can show. So this carries a model
// of Uc8279Driver's flag machine -- enough of it to report, per refresh, which bank
// loaded and whether DTM1 was seeded white.
//
// THE MODEL IS A SECOND COPY OF PRIVATE STATE AND THAT IS THIS DIRECTORY'S LARGEST
// FIDELITY RISK. `_oldPlaneValid`, `_forceFullSyncNext`, `_initialFullsRemaining`,
// `_lsbValid` and `_inGrayscaleMode` are private in Uc8279Driver.h with no
// accessor, and freeink-sdk/ must never be edited -- so NO runner can read them
// back and nothing here can tell you the model is still right. `make firmware`
// checks the signatures on every PR and says nothing about the semantics.
//
// WHICH IS WHY THE INVARIANT BELOW MATTERS MORE THAN THE MODEL. It is about what
// the SHELL knows, it needs no model at all, and it survives any SDK change.
class FreeInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum GrayPlane { GRAY_PLANE_LSB, GRAY_PLANE_MSB };

  // THE PINS ARE RECORDED, NOT KEPT. Nothing here drives a bus, so storing them
  // would be six fields no line reads -- but the panel's wiring is worth seeing in
  // a transcript, because a board profile that selected the wrong one is a fault
  // that looks like a dead panel.
  FreeInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy) {
    harness::record("<panel> pins sclk=%d mosi=%d cs=%d dc=%d rst=%d busy=%d", sclk, mosi, cs,
                    dc, rst, busy);
  }

  // The X3's panel, which is the dev device: 792x528 landscape, rendered as a
  // 528x792 portrait canvas after the CCW rotation core/ applies.
  static constexpr uint16_t kWidth = 792;
  static constexpr uint16_t kHeight = 528;

  void begin() {
    frame_.assign(static_cast<size_t>(kWidth) * kHeight / 8, 0x00);
    // initController(): the baseline is unknown and two full refreshes are owed.
    oldPlaneValid_ = false;
    forceFullSyncNext_ = false;
    initialFullsRemaining_ = 2;
    refreshesSinceBegin_ = 0;
    harness::record("<panel> begin %ux%u", kWidth, kHeight);
  }

  // REAL MEMORY, AND THAT IS A FEATURE. bindFrameToDriver refuses a view whose
  // size disagrees with getBufferSize(), so the fake has to be dimensionally
  // honest -- and in exchange every render really writes pixels, which makes the
  // shell's paint path golden-testable for the first time.
  uint8_t* getFrameBuffer() const { return const_cast<uint8_t*>(frame_.data()); }
  uint32_t getBufferSize() const { return static_cast<uint32_t>(frame_.size()); }
  uint16_t getDisplayWidth() const { return kWidth; }
  uint16_t getDisplayHeight() const { return kHeight; }

  void triggerDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false) {
    (void)turnOffScreen;
    pendingMode_ = mode;
    // displayStart's own expression, and it is an OR: a Fast refresh still loads
    // the GC bank when the baseline is unknown, a resync is owed, or a boot clear
    // is still outstanding.
    pendingGc_ = (mode != FAST_REFRESH) || !oldPlaneValid_ || forceFullSyncNext_ ||
                 initialFullsRemaining_ > 0;
    pendingSeedWhite_ = !oldPlaneValid_;
    harness::record("<panel> refresh bank=%s seed=%s mode=%s", pendingGc_ ? "GC" : "DU",
                    pendingSeedWhite_ ? "white" : "none", modeName(mode));
    harness::clock_().advance(pendingGc_ ? 693 : 389);
  }

  void completeDisplay() {
    // displayFinish: DTM1 now holds what is on the glass.
    oldPlaneValid_ = true;
    forceFullSyncNext_ = false;
    if (initialFullsRemaining_ > 0) --initialFullsRemaining_;
    ++refreshesSinceBegin_;
    harness::record("<panel> refresh-complete");
  }

  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false) {
    (void)turnOffScreen;
    harness::record("<panel> gray-base %s", modeName(fallback));
    harness::clock_().advance(367);
    oldPlaneValid_ = false;  // the grayscale sequence leaves DTM1 invalid until the rebase
    ++refreshesSinceBegin_;
  }
  void preconditionGrayscale() {
    harness::record("<panel> gray-settle");
    harness::clock_().advance(156);
  }
  void copyGrayscaleLsbBuffers(const uint8_t* lsb) {
    harness::record("<panel> gray-plane lsb=%s", lsb != nullptr ? "yes" : "NULL");
    lsbSeen_ = lsb != nullptr;
  }
  void copyGrayscaleMsbBuffers(const uint8_t* msb) {
    // The driver drops an MSB copy it has not seen a valid LSB for, silently --
    // so LSB-BEFORE-MSB is a real ordering rule and the transcript shows it.
    harness::record("<panel> gray-plane msb=%s%s", msb != nullptr ? "yes" : "NULL",
                    lsbSeen_ ? "" : " (NO LSB SEEN -- the driver drops this)");
  }
  void displayGrayBuffer(bool turnOffScreen = false, const unsigned char* lut = nullptr,
                         bool factoryMode = false) {
    (void)turnOffScreen;
    (void)lut;
    (void)factoryMode;
    harness::record("<panel> gray-show");
    harness::clock_().advance(366);
  }
  void cleanupGrayscaleBuffers(const uint8_t* bw) {
    harness::record("<panel> gray-rebase bw=%s", bw != nullptr ? "yes" : "NULL");
    oldPlaneValid_ = true;  // the rebase puts the controller back on a valid B/W baseline
    lsbSeen_ = false;
    ++refreshesSinceBegin_;
  }

  bool combinesGrayscaleBase() const { return false; }
  bool supportsStripGrayscale() const { return true; }
  bool supportsBusyGrayscaleStaging() const { return false; }

  void requestResync(uint8_t settlePasses = 0) {
    forceFullSyncNext_ = true;
    harness::record("<panel> requestResync settle=%u", settlePasses);
  }

  // THE CALLER-SIDE INVARIANT, AND IT NEEDS NO MODEL.
  //
  // skipInitialResync() reads as an optimisation you may take. It is a CLAIM: that
  // the controller's baseline already matches the glass. #94 was that claim made
  // falsely -- an `if (!coverOnGlass) display.skipInitialResync();` sitting 661
  // lines after begin() with no refresh in between, which left the waveform diffing
  // against power-up garbage and put noisy banding on every DETAILS-mode wake.
  //
  // No caller may make it unless THIS PROCESS has completed a refresh since the last
  // begin(). Both live call sites pass -- one after showOnePass, one after
  // renderTop -- and the #94 form cannot. That is a fact about what the shell owns,
  // so it survives any SDK change.
  void skipInitialResync() {
    if (refreshesSinceBegin_ == 0) {
      harness::record(
          "<panel> INVARIANT VIOLATED: skipInitialResync() with no refresh since begin() "
          "-- this is #94's shape: the baseline is power-up garbage and the claim is false");
    } else {
      harness::record("<panel> baseline-asserted (after %d refresh(es))", refreshesSinceBegin_);
    }
    oldPlaneValid_ = true;
    initialFullsRemaining_ = 0;
  }

  void deepSleep() { harness::record("<panel> deepSleep"); }
  void setDisplayX3() { harness::record("<panel> setDisplayX3"); }

  // For a scenario that wants to assert the modelled state directly rather than
  // through the transcript.
  bool baselineValid() const { return oldPlaneValid_; }
  int refreshesSinceBegin() const { return refreshesSinceBegin_; }

 private:
  static const char* modeName(RefreshMode m) {
    return m == FULL_REFRESH ? "FULL" : m == HALF_REFRESH ? "HALF" : "FAST";
  }

  std::vector<uint8_t> frame_;
  bool oldPlaneValid_ = false;
  bool forceFullSyncNext_ = false;
  int initialFullsRemaining_ = 2;
  int refreshesSinceBegin_ = 0;
  bool lsbSeen_ = false;
  bool pendingGc_ = false;
  bool pendingSeedWhite_ = false;
  RefreshMode pendingMode_ = FAST_REFRESH;
};

}  // namespace freeink
