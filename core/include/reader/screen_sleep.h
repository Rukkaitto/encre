#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// design/Sleep.dc.html: what the panel holds while the device is asleep.
//
// IT TAKES NO INPUT, and that is not an omission. The shell paints this and then
// calls deep sleep, so there is nobody left to press anything -- the only way out
// is the power button, which is a hardware wake and not an event this screen could
// see. onEvent therefore answers none() for everything, including Back: a screen
// that could be dismissed would imply a running device.
//
// It also draws NO HINT BAR, for the same reason. The bar is a contract about what
// the four front buttons do, and while the device is asleep they do nothing.
//
// E-ink is why this screen exists at all: the glass holds its last image with no
// power, so whatever is painted before sleeping is what the user sees for as long
// as the device is off. Leaving the previous screen there would show a Library or a
// half-read page and give no clue the device is asleep rather than frozen -- which
// this project has confused itself once already (CLAUDE.md: "a frozen screen does
// not mean the firmware ran").
class SleepScreen : public Screen {
 public:
  explicit SleepScreen(SleepViewModel vm);

  ScreenId id() const override { return ScreenId::Sleep; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const SleepViewModel& vm() const { return vm_; }

 private:
  SleepViewModel vm_;
};

}  // namespace reader
