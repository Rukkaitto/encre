#pragma once
#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// The no-card prompt (spec 6: "No/failed SD card: full-screen prompt with retry;
// device never boots into a broken UI"), from design/SdMissing.dc.html.
//
// It is the app's ROOT when there is no usable card, not a screen pushed over
// Home: there is nothing behind it to go back to, which is why Back is an empty
// hint slot and why the board draws three of its four slots as placeholders.
//
// Confirm is the retry. The screen does not mount anything -- core/ has no card
// and no filesystem -- so it returns Action::retry() and the shell re-attempts
// the mount; see App::retryRequested() for the contract.
class SdMissingScreen : public Screen {
 public:
  SdMissingScreen();

  ScreenId id() const override { return ScreenId::SdMissing; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const SdMissingViewModel& vm() const { return vm_; }

 private:
  SdMissingViewModel vm_;
};

}  // namespace reader
