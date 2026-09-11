#include "reader/screen_wifi_error.h"

#include "reader/theme.h"

namespace reader {
namespace {

constexpr const char* kOpenQuote = "\xE2\x80\x9C";
constexpr const char* kCloseQuote = "\xE2\x80\x9D";
// The typographic apostrophe the boards use; a straight one is a different
// glyph and the goldens would show it.
constexpr const char* kApos = "\xE2\x80\x99";

// THE THREE SENTENCES, and all three end "Wi-Fi is off again." because that is
// true on every path -- which is what makes it worth saying once in one place.
//
// EVERY ONE WAS MEASURED AGAINST THE WRAP BOUNDARY IN BOTH DIRECTIONS before
// it was written here. The firmware's .rfnt faces measure ~3% wider than
// Chrome's, so a line that merely fits on the board wraps differently on
// glass, the centred panel grows, and every rule inside it lands out of
// register -- #76's defect, which cost 11.12% against 3.58%. The shipped
// wording clears by at least 4% of the column both ways; see the boards.
//
// THE FIRST DOES NOT LEAD WITH THE SSID where the other two do, and that is a
// trade forced by measurement rather than a preference: every SSID-first
// wording tried sat inside the floor in one direction or the other.
std::string sentence(JoinFailure why, const std::string& ssid) {
  const std::string quoted = std::string(kOpenQuote) + ssid + kCloseQuote;
  switch (why) {
    case JoinFailure::BadPassword:
      return "Wrong password for " + quoted + ". Wi-Fi is off again.";
    case JoinFailure::NotFound:
      return quoted + " didn" + kApos + "t answer. It may be out of range. Wi-Fi is off again.";
    case JoinFailure::Incomplete:
      break;
  }
  return quoted + " took the password but never finished connecting. Wi-Fi is off again.";
}

}  // namespace

int WifiErrorScreen::actionsFor(JoinFailure why) {
  return why == JoinFailure::BadPassword ? 3 : 2;
}

WifiErrorScreen::WifiErrorScreen(std::string ssid, JoinFailure why)
    : FocusScreen(actionsFor(why), actionsFor(why)), ssid_(std::move(ssid)), why_(why) {
  // ONE CAPTION FOR ALL THREE, as BookError's three shapes share `CAN'T OPEN
  // FILE`: the caption names the event and the sentence names the cause.
  vm_.caption = std::string("COULDN") + kApos + "T JOIN";
  vm_.message = sentence(why_, ssid_);
  vm_.offersEdit = (why_ == JoinFailure::BadPassword);
  if (vm_.offersEdit) vm_.actions.push_back("EDIT PASSWORD");
  vm_.actions.push_back("TRY AGAIN");
  vm_.actions.push_back("CANCEL");
  syncVm();
}

void WifiErrorScreen::syncVm() {
  vm_.focusedAction = focus();
  // SELECT rather than a label naming one slab, because the focus moves
  // between two or three of them -- unlike BookError's single-row shape, where
  // the Confirm hint can name the slab it activates.
  vm_.hints = {"CANCEL", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

Action WifiErrorScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      chosen_ = Chosen::Cancel;
      return Action::pop();
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Activate: {
      // Indexed off the slab list rather than off `offersEdit`, so the two
      // cannot disagree about which slab row 0 is.
      const int f = focus();
      if (f < 0 || f >= static_cast<int>(vm_.actions.size())) return Action::none();
      const std::string& label = vm_.actions[static_cast<size_t>(f)];
      if (label == "EDIT PASSWORD") {
        chosen_ = Chosen::EditPassword;
      } else if (label == "TRY AGAIN") {
        chosen_ = Chosen::TryAgain;
      } else {
        chosen_ = Chosen::Cancel;
      }
      return Action::pop();
    }
    default:
      return Action::none();
  }
}

void WifiErrorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderWifiError(fb, fonts, vm_, plane);
}

}  // namespace reader
