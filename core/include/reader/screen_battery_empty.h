#pragma once

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// design/BatteryEmpty.dc.html -- what the panel holds after a critical shutdown.
//
// A SCREEN RATHER THAN A SPECIAL CASE IN THE SHELL, for SleepScreen's reason: the
// simulator and the goldens can then render it like everything else, and `make
// compare` can measure it against its board.
//
// PAINTED DIRECTLY AND NEVER PUSHED, also for SleepScreen's reason -- the session
// record names the top of the stack, so pushing it would make the next wake restore
// INTO it. The shell's paint bypasses App, which means it owns the two things App
// normally does: the clear, and gFrameContentsUnknown.
class BatteryEmptyScreen : public Screen {
 public:
  BatteryEmptyScreen();

  ScreenId id() const override { return ScreenId::BatteryEmpty; }
  // NOBODY IS LEFT TO PRESS ANYTHING: this is painted and then the chip stops.
  // Back answers none() too, which is one of the two places on this device it does
  // (SleepScreen is the other, for the same reason).
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  // ONE WAVEFORM. There is no photograph here and no body text, so four levels
  // would spend three waveforms on a pack that has none to spend.
  Fidelity fidelity() const override { return Fidelity::Mono; }

  const BatteryEmptyViewModel& vm() const { return vm_; }

 private:
  BatteryEmptyViewModel vm_;
};

}  // namespace reader
