#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"
#include "reader/wifi_store.h"

namespace reader {

// WHERE A CHANGED NETWORK LIST GOES. SettingsSink's shape and its argument: an
// interface rather than a std::function, because -fno-exceptions makes a
// std::function's allocation an abort() with no diagnostic, and the one
// implementer is the shell, which owns NVS.
//
// It both APPLIES and PERSISTS, like SettingsSink::commit. False means the
// write failed; the screen still shows the new state, because the change has
// taken effect in RAM and reverting the display would make a failed write look
// like a screen that ignores its buttons.
class WifiSink {
 public:
  virtual ~WifiSink() = default;
  virtual bool commit(const SavedNetworks& nets) = 0;
};

// design/WifiSettings.dc.html, and design/WifiSettingsEmpty.dc.html as a
// VARIANT rather than a second screen -- same ScreenId, same view model, copy
// where the saved list would be. HomeEmpty's mechanism.
//
// THE HOLD IS THE SECOND ONE IN THIS FIRMWARE. The Library's is the first, and
// the binding is identical: Confirm short acts on the row, Confirm long opens
// an actions overlay. Spec 4.0 says so in as many words, and the board has
// drawn the ring on the SELECT slot since it was authored.
//
// SELECT TOGGLES AUTO IN PLACE rather than opening anything, which is Settings'
// own CHANGE idiom. That leaves the hold free for the one destructive action,
// and it is why the row needs no chevron.
//
// THE FOCUS SKIPS THE SECTION HEADERS, via FocusScreen's gate -- Settings'
// mechanism, not a second copy of it.
class WifiSettingsScreen : public FocusScreen {
 public:
  // `sink` may be null: the simulator and the goldens have nowhere to persist,
  // exactly as SettingsSink may be null there.
  WifiSettingsScreen(SavedNetworks nets, WifiSink* sink);

  ScreenId id() const override { return ScreenId::WifiSettings; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const WifiSettingsViewModel& vm() const { return vm_; }
  const SavedNetworks& networks() const { return nets_; }

  // Which network the focused row names, or empty when the focus is not on one
  // -- the SETUP row, or an empty list. The shell reads this to prime the
  // actions overlay, and it is a query rather than a stored field so it cannot
  // go stale behind a focus move.
  std::string focusedSsid() const;

 protected:
  bool focusable(int index) const override;
  void syncVm() override;

 private:
  // Rebuilds the row table from `nets_`. Called whenever the list changes,
  // because the rows ARE the list and a screen holding a stale copy of one is
  // the defect Home and the Library each shipped once.
  void rebuild();

  SavedNetworks nets_;
  WifiSink* sink_ = nullptr;
  WifiSettingsViewModel vm_;
  // Parallel to vm_.rows: the SSID a row names, empty for a header or the
  // SETUP row. Kept here rather than in the view-model because it is not
  // something the theme draws.
  std::vector<std::string> rowSsid_;
};

}  // namespace reader
