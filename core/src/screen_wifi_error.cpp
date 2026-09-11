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

WifiErrorScreen::WifiErrorScreen(std::string ssid, JoinFailure why)
    // ZERO, AND THE REAL RANGE BELOW -- WifiSettingsScreen's own shape, and
    // here it is what makes the count honest. This was
    // `FocusScreen(actionsFor(why), actionsFor(why))` over a static
    // `why == BadPassword ? 3 : 2`, and the header claimed "THE ROW COUNT IS
    // THE ONLY GATE". There were THREE spellings of that count -- actionsFor,
    // `offersEdit`, and the unconditional push_back pair -- and nothing tied
    // them, so each could drift from the others:
    //
    //   actionsFor -> 2 : the BadPassword dialog DRAWS three slabs and the
    //                     focus can never reach CANCEL.
    //   actionsFor -> 3 : the two-slab shapes get a focus position past the
    //                     last slab. renderWifiError highlights
    //                     `i == vm.focusedAction`, so two Downs leave NOTHING
    //                     selected and Activate there returns none() -- a dead
    //                     Confirm on a live dialog.
    //
    // Both survived the whole suite, because all three error goldens render
    // focus 0.
    : FocusScreen(0, 0), ssid_(std::move(ssid)), why_(why) {
  // ONE CAPTION FOR ALL THREE, as BookError's three shapes share `CAN'T OPEN
  // FILE`: the caption names the event and the sentence names the cause.
  vm_.caption = std::string("COULDN") + kApos + "T JOIN";
  vm_.message = sentence(why_, ssid_);
  vm_.offersEdit = (why_ == JoinFailure::BadPassword);
  if (vm_.offersEdit) vm_.actions.push_back("EDIT PASSWORD");
  vm_.actions.push_back("TRY AGAIN");
  vm_.actions.push_back("CANCEL");
  // THE SLAB LIST IS THE COUNT. Built first, then measured -- so the focus
  // range cannot disagree with what is drawn, and `offersEdit` is reduced to
  // what it always should have been: a fact about ONE slab, consumed by the
  // push_back above it and by the theme. There is nothing left for a second
  // condition to drift from.
  const int slabs = static_cast<int>(vm_.actions.size());
  window().setCount(slabs);
  window().setVisibleRows(slabs);
  window().setFocus(0, nullptr);
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
      // Back IS Cancel here -- the bar says so -- and Cancel is giving up on
      // this network, which the shell has to act on. Latched rather than
      // popped: chosen() is unreadable from a destroyed screen.
      chosen_ = Chosen::Cancel;
      return Action::wifi();
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
      return Action::wifi();
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
