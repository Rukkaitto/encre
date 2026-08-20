#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// A diagnostic that prints the classified presses it receives.
//
// This is what proves the phase on hardware. Short-versus-long classification
// and the FAST refresh path are both invisible in a serial log and both visible
// here: press Confirm and read SHORT, hold it and read LONG, at panel speed.
// Fidelity::Mono on purpose -- a diagnostic is the one surface that can afford
// thresholded text, and using it exercises the fast path in this phase rather
// than leaving it untested until the Reader lands.
class InputMonitorScreen : public Screen {
 public:
  InputMonitorScreen();

  ScreenId id() const override { return ScreenId::InputMonitor; }
  Fidelity fidelity() const override { return Fidelity::Mono; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // The log is the whole point of this screen, so it is readable: a test that
  // had to infer it from pixels would be asserting the theme, not the screen.
  const StubViewModel& vm() const { return vm_; }

 private:
  static constexpr size_t kMaxLines = 8;

  StubViewModel vm_;
};

}  // namespace reader
