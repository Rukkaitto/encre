#pragma once
#include <cstddef>
#include <string>

#include "reader/screen_text_entry.h"

namespace reader {

// design/WifiPassword.dc.html -- the passphrase keyboard, and the first caller
// of TextEntryScreen.
//
// THE KEYBOARD ITSELF IS NO LONGER HERE. It was the first text entry in this
// firmware and #111 is about to be the second caller, so it was extracted
// before the second copy rather than five copies late -- see
// screen_text_entry.h, which holds the grid, the caret, the modifiers, the
// input model and every string a keyboard can own. What is left in this file is
// exactly what is Wi-Fi-shaped, and all of it is 802.11's or the shell's.
//
// THE FLOOR IS THE ONE THING THAT MAY NOT BE GENERALISED. It makes the confirm
// cell inert and blanks the Confirm slot, which is right for a passphrase that
// physically cannot join and wrong for any field where an empty value is legal
// -- so TextEntryConfig::minLength defaults to 0 and this is the only call site
// that sets it. See its comment for the case that named the trap.
class WifiPasswordScreen : public TextEntryScreen {
 public:
  // 802.11's own bound on a WPA2 passphrase. Typing stops here rather than
  // silently dropping characters, because a keyboard that swallows a keypress
  // is indistinguishable from one that missed it.
  static constexpr size_t kMaxPassphrase = 63;
  // AND ITS FLOOR, from the same clause of 802.11 and spec 4.1b's own "8 to
  // 63". This keyboard is only ever reached for a LOCKED network -- an open
  // one joins directly -- so JOIN under eight characters cannot succeed: it
  // would spend a radio round trip and a failure dialog to report a length
  // the counter is already showing.
  static constexpr size_t kMinPassphrase = 8;

  explicit WifiPasswordScreen(std::string ssid);

  ScreenId id() const override { return ScreenId::WifiPassword; }

  // Whether JOIN was pressed. The shell starts the join; this screen owns no
  // radio -- and it reads entered() off this same object, which is the
  // sharpest reason the pop had to go: the pop destroyed the passphrase.
  // READ IT WHILE THIS SCREEN IS STILL ON TOP; see TextEntryScreen::confirmed(),
  // which this names in Wi-Fi's own words for the shell that already reads it.
  bool joinChosen() const { return confirmed(); }
};

}  // namespace reader
