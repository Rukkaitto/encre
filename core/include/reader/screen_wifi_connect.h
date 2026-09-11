#pragma once
#include <string>

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// design/WifiConnect.dc.html -- the connecting dialog.
//
// IT REPLACES THE JOIN STACK RATHER THAN SITTING ON IT. The shell pushes it
// with Action::replace, which pushes before it erases so a refused factory
// leaves the stack intact. That is what makes ONE veiled parent truthful for
// both entry paths: a locked network arrives here from WifiPassword and an
// OPEN one arrives straight from WifiPicker, so an open network has no
// WifiPassword to veil. WifiError already veils WifiSettings, so the pair
// agrees with no board change to the error screen's background.
//
// NO FOCUS AND NO FocusScreen. There is one action and it is CANCEL, on Back --
// nothing here is selectable, so a focus would be a position with nothing at
// the other end of it.
//
// THE CAPTION IS THE WHOLE INDICATOR. The board's eight-cell ticker is gone: it
// read as a fraction of a known total, a join takes an unknown one to ten
// seconds, and nothing on this device animates -- every advance would be a
// ~520 ms waveform. CONNECTING... steps to READY, two paints for a successful
// join, and the ellipsis is already the indeterminate mark. Same call as the
// reader's em dash over a literal zero.
class WifiConnectScreen : public Screen {
 public:
  // `ssid` names the network in the message. There is no passphrase here and
  // there never is: this screen reports, it does not hold a credential.
  explicit WifiConnectScreen(std::string ssid);

  ScreenId id() const override { return ScreenId::WifiConnect; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const WifiConnectViewModel& vm() const { return vm_; }

  // The shell steps the label when the radio reports success, just before it
  // takes the radio down and pops. Returns whether anything changed, so a
  // second call cannot cost a ~520 ms repaint.
  bool markReady();
  bool ready() const { return ready_; }

  // Whether the reader asked to stop. The shell reads it after the pop and
  // takes the radio down.
  bool cancelled() const { return cancelled_; }

 private:
  void syncVm();

  std::string ssid_;
  WifiConnectViewModel vm_;
  bool ready_ = false;
  bool cancelled_ = false;
};

}  // namespace reader
