#include "reader/screen_wifi_password.h"

#include <string>
#include <utility>

namespace reader {
namespace {

TextEntryConfig configFor(std::string ssid) {
  TextEntryConfig c;
  c.title = "PASSWORD";
  // THE BAND'S RIGHT SLOT -- the network this passphrase is for. The generic
  // name for the slot is a field name, which is what a caller with more than
  // one field puts there; Wi-Fi has one field and a network, so it names the
  // network.
  c.fieldName = std::move(ssid);
  c.confirmLabel = "JOIN";
  // SHOWN IN CLEAR, which is the board's `SHOWN WHILE TYPING`: at one
  // character per ~520 ms repaint on a 46-cell grid, a typo you cannot see is
  // punishing, and this is a device you hold. It is a Wi-Fi decision about a
  // passphrase you own rather than a property of typing, which is why the
  // string is set here and not in the mechanism.
  c.visibility = "SHOWN WHILE TYPING";
  c.maxLength = WifiPasswordScreen::kMaxPassphrase;
  // THE ONLY CALL SITE IN THE FIRMWARE THAT SETS A FLOOR, and 802.11 is the
  // whole of the reason. See TextEntryConfig::minLength.
  c.minLength = WifiPasswordScreen::kMinPassphrase;
  // BOTH OUTCOMES ARE THE SAME LATCH, because the shell tells them apart by
  // joinChosen() and cancelled() rather than by the Action, and both need the
  // passphrase off a screen that is still standing. Action::wifi() pops
  // nothing, so the shell reads the outcome on the dispatch's own pass and
  // pops afterwards -- see Action::wifi() and App::wifiRequested().
  c.confirmAction = Action::wifi();
  c.leaveAction = Action::wifi();
  return c;
}

}  // namespace

WifiPasswordScreen::WifiPasswordScreen(std::string ssid)
    : TextEntryScreen(configFor(std::move(ssid))) {}

}  // namespace reader
