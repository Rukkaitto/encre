#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"
#include "reader/wifi_radio.h"

namespace reader {

// design/WifiPicker.dc.html, plus its scrolled and empty variants.
//
// THE SECOND SCROLLING LIST IN THE FIRMWARE, and the second user of the rail.
// CLAUDE.md's rail section says it "governs every scrollable list, and today
// that is Library alone" -- and its own history named this screen there until
// Wi-Fi was cut and the mention went with it.
//
// IT TAKES ROWS, NOT A RADIO. The shell drives the scan and hands over what
// rankScanResults produced -- deduplicated, ranked and capped -- because that
// is a decision about what to draw and it is tested as a free function. A
// screen holding a radio would be the only screen in this firmware that holds
// a device, and the Library takes items rather than a FileSystem for the same
// reason.
//
// RESCAN IS A ROW, NOT A SLAB, because the board draws it as one -- and being
// a row is what lets the Confirm hint name it when it is focused, which is
// Settings' rule.
class WifiPickerScreen : public FocusScreen {
 public:
  // `visibleRows` is the theme's measurement, so it is passed in rather than
  // derived here: three defects in this project came from pinning a number the
  // board computes. Zero means "not told yet" and refuses movement, which is
  // FocusScreen's own rule.
  WifiPickerScreen(std::vector<ScanResult> rows, int visibleRows);

  ScreenId id() const override { return ScreenId::WifiPicker; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const WifiPickerViewModel& vm() const { return vm_; }

  // While this is set the list is replaced by drawStatusBar's centred line and
  // input does nothing but Back -- the scan has not come back. Returns whether
  // anything changed.
  bool setScanning(bool on);

  // A finished scan. Replaces the rows and puts the focus back at the top,
  // because the list the old focus indexed no longer exists.
  void setResults(std::vector<ScanResult> rows);

  void setVisibleRows(int n);

  // What the reader chose, once Confirm has fired on a network row. Empty
  // until then, and empty on the Rescan row -- which is why rescanChosen() is
  // separate rather than being an empty SSID.
  //
  // These three were readable, because this screen never popped -- but the
  // Action that carried the choice was `Action::redraw()`, a SIDE CHANNEL that
  // spent a ~520 ms repaint of an identical frame and gave the shell no signal
  // to look at all. They latch Action::wifi() now, which is the signal, and
  // still pop nothing. See Action::wifi().
  const std::string& chosenSsid() const { return chosen_; }
  bool chosenLocked() const { return chosenLocked_; }
  bool rescanChosen() const { return rescan_; }
  void clearChoice();

 protected:
  void syncVm() override;

 private:
  void rebuild();
  // The three-bar glyph the board draws, banded HERE rather than in the theme:
  // which mark a signal deserves is a decision, and a view-model carrying dBm
  // would push that decision into the renderer.
  static int barsFor(int rssi);

  std::vector<ScanResult> results_;
  WifiPickerViewModel vm_;
  std::string chosen_;
  bool chosenLocked_ = false;
  bool rescan_ = false;
  bool scanning_ = false;
};

}  // namespace reader
