#include "reader/screen_wifi_connect.h"

#include "reader/theme.h"

namespace reader {
namespace {

// The board's own copy. "to test the password" rather than "to receive books":
// this release has nothing to receive, the join exists to prove the
// credential, and a sentence naming a transfer that cannot happen is the
// false-claim shape this project refuses elsewhere.
//
// The quotes are the board's typographic ones, as UTF-8 bytes.
constexpr const char* kOpenQuote = "\xE2\x80\x9C";
constexpr const char* kCloseQuote = "\xE2\x80\x9D";

// "AGAIN WHEN THIS FINISHES" rather than "WHEN THE TRANSFER FINISHES", which
// is true now AND stays true when transfer lands -- so the sentence does not
// have to be rewritten twice.
constexpr const char* kNote = "WI-FI TURNS OFF AGAIN AFTERWARDS.";

}  // namespace

WifiConnectScreen::WifiConnectScreen(std::string ssid) : ssid_(std::move(ssid)) {
  syncVm();
}

void WifiConnectScreen::syncVm() {
  vm_.caption = "CONNECTING\xE2\x80\xA6";
  vm_.right = "WI-FI";
  // ONE CAPTION AND ONE SENTENCE, because there is one state. Both stepped to
  // a READY the shell can no longer reach: it leaves for the saved-network
  // list the moment the radio reports success, and that list -- with the
  // network in it -- is the confirmation READY was trying to be, without the
  // second waveform. See the header.
  vm_.message = std::string("Joining ") + kOpenQuote + ssid_ + kCloseQuote +
                " to test the password.";
  vm_.note = kNote;
  // One live slot. The other three are empty, which the theme draws at
  // kHintEmptySlotW rather than as nothing -- measuring a dead slot as zero
  // moves the live one.
  vm_.hints = {"CANCEL", "", "", ""};
  vm_.holds = {false, false, false, false};
}

Action WifiConnectScreen::onGesture(const GestureEvent& g) {
  if (g.what != Gesture::Back) return Action::none();
  // NOT NAVIGATION: a join is in flight, so the radio has to be told. Latched
  // rather than popped, both because the shell has work to do and because
  // cancelled() cannot be read off a screen the pop has destroyed.
  cancelled_ = true;
  return Action::wifi();
}

void WifiConnectScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                               Plane plane) const {
  theme.renderWifiConnect(fb, fonts, vm_, plane);
}

}  // namespace reader
