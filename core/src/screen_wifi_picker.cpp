#include "reader/screen_wifi_picker.h"

#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

constexpr const char* kNote = "OPEN NETWORKS JOIN DIRECTLY; LOCKED ONES ASK FOR A PASSWORD.";
constexpr const char* kEmptyTitle = "Nothing answered";
constexpr const char* kEmptyProse = "Move closer and scan again.";
// THE 2.4 GHz CAVEAT, and whether it belongs on this screen at all is the one
// question design/WifiPickerEmpty.dc.html leaves open: a dual-band household
// never sees this state, and a 5 GHz-only one sees a POPULATED list with the
// name they want missing.
constexpr const char* kEmptyCaveat =
    "Encre joins 2.4 GHz networks only. A 5 GHz network never appears here.";
constexpr const char* kRescan = "Rescan";

}  // namespace

int WifiPickerScreen::barsFor(int rssi) {
  // The usual three-band split for a 2.4 GHz client. Bands rather than a
  // percentage because the board draws three marks and nothing finer would
  // reach the glass.
  if (rssi >= -55) return 3;
  if (rssi >= -70) return 2;
  return 1;
}

WifiPickerScreen::WifiPickerScreen(std::vector<ScanResult> rows, int visibleRows)
    : FocusScreen(static_cast<int>(rows.size()) + 1, visibleRows), results_(std::move(rows)) {
  rebuild();
}

void WifiPickerScreen::rebuild() {
  vm_.title = "JOIN NETWORK";
  vm_.scanning = scanning_;
  vm_.statusLabel = "SCANNING";
  vm_.nothingFound = results_.empty();
  vm_.emptyTitle = kEmptyTitle;
  vm_.emptyProse = kEmptyProse;
  vm_.emptyCaveat = kEmptyCaveat;
  vm_.note = kNote;
  // `NONE FOUND` rather than `0 FOUND`. Zero is a real value in a count slot,
  // so this is not a false claim -- it is simply colder than it needs to be,
  // and this is the one screen where the number carries nothing the words
  // below do not.
  vm_.found = results_.empty() ? "NONE FOUND" : std::to_string(results_.size()) + " FOUND";
  // The Rescan row is the last item of the list, so it scrolls with it and is
  // counted by the rail -- treating it as chrome would make the rail lie about
  // how much list there is.
  window().setCount(static_cast<int>(results_.size()) + 1);
  syncVm();
}

void WifiPickerScreen::syncVm() {
  const ScrollWindow::Slice s = window().slice();
  vm_.rows.clear();
  for (int i = 0; i < s.count; ++i) {
    const int at = s.first + i;
    WifiScanRow row;
    if (at == static_cast<int>(results_.size())) {
      row.isRescan = true;
      row.ssid = kRescan;
    } else {
      const ScanResult& r = results_[static_cast<size_t>(at)];
      row.ssid = r.ssid;
      row.bars = barsFor(r.rssi);
      row.locked = r.locked;
    }
    vm_.rows.push_back(row);
  }
  vm_.focusedRow = s.focused;
  vm_.firstRow = window().firstVisible();
  vm_.totalRows = window().count();
  // THE CONFIRM LABEL FOLLOWS THE FOCUSED ROW -- Settings' rule. On the Rescan
  // row there is nothing to join, so a bar reading JOIN would name a button
  // that does something else.
  const bool onRescan = focus() == static_cast<int>(results_.size());
  const bool anyRows = !results_.empty();
  vm_.hints = {"BACK", onRescan ? "RESCAN" : "JOIN", anyRows ? "UP" : "", anyRows ? "DOWN" : ""};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // Up and Down repeat, as the Library's and Contents' do: a dense building
  // returns twenty rows and a press per row is not a way to reach the last.
  declareRepeat(buttonBit(Button::Up) | buttonBit(Button::Down));
}

bool WifiPickerScreen::setScanning(bool on) {
  if (scanning_ == on) return false;
  scanning_ = on;
  rebuild();
  return true;
}

void WifiPickerScreen::setResults(std::vector<ScanResult> rows) {
  results_ = std::move(rows);
  scanning_ = false;
  // The old focus indexed a list that no longer exists, so it goes back to the
  // top rather than landing on whatever now occupies that row.
  window().setCount(static_cast<int>(results_.size()) + 1);
  window().setFocus(0, nullptr);
  rebuild();
}

void WifiPickerScreen::setVisibleRows(int n) {
  window().setVisibleRows(n);
  syncVm();
}

void WifiPickerScreen::clearChoice() {
  chosen_.clear();
  chosenLocked_ = false;
  rescan_ = false;
}

Action WifiPickerScreen::onGesture(const GestureEvent& g) {
  // A scan in flight owns the screen: the list is not there to move around in,
  // and the bar promises nothing but Back.
  if (scanning_) {
    if (g.what == Gesture::Back) return Action::pop();
    return Action::none();
  }
  switch (g.what) {
    case Gesture::Back:
      return Action::pop();
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Activate: {
      const int f = focus();
      if (f < 0) return Action::none();
      if (f == static_cast<int>(results_.size())) {
        rescan_ = true;
        // The shell starts the scan and calls setScanning; this screen does
        // not own the radio.
        //
        // A LATCH RATHER THAN A REDRAW, and the redraw was a side channel:
        // this screen's outcomes were readable only because it never popped,
        // so the shell would have had to poll them after every dispatch -- and
        // the Action it returned spent a ~520 ms repaint of an IDENTICAL frame
        // to carry the signal. Nothing is dirty here: what a rescan changes on
        // glass is setScanning's to mark.
        return Action::wifi();
      }
      const ScanResult& r = results_[static_cast<size_t>(f)];
      chosen_ = r.ssid;
      chosenLocked_ = r.locked;
      // AN OPEN NETWORK JOINS DIRECTLY and a locked one asks for a password,
      // which is the board's own note. The shell reads chosenLocked() and
      // pushes one or the other, because which screen comes next depends on
      // state this screen does not have -- whether a passphrase is already
      // stored for it.
      //
      // Latched for the rescan row's reason, and it matters more here: the
      // repaint this used to spend was in front of the push that replaces the
      // screen being repainted.
      return Action::wifi();
    }
    default:
      return Action::none();
  }
}

void WifiPickerScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                              Plane plane) const {
  theme.renderWifiPicker(fb, fonts, vm_, plane);
}

}  // namespace reader
