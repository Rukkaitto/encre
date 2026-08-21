#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// A diagnostic that prints the classified presses it receives.
//
// This is what proves the phase on hardware. Short-versus-long classification
// and the FAST refresh path are both invisible in a serial log and both visible
// here: press Confirm and read SHORT, hold it and read LONG, at panel speed.
//
// It declares no fidelity: the inherited default is Fidelity::Mono, which is the
// fast path this screen exists to exercise. It used to override to `Mono`
// explicitly, back when chrome was on the grayscale path and a diagnostic was the
// one surface that could afford a cheap refresh; now every screen takes it.
class InputMonitorScreen : public Screen {
 public:
  InputMonitorScreen();

  ScreenId id() const override { return ScreenId::InputMonitor; }
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
