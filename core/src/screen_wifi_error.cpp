#include "reader/screen_wifi_error.h"

#include <array>
#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

constexpr const char* kOpenQuote = "\xE2\x80\x9C";
constexpr const char* kCloseQuote = "\xE2\x80\x9D";
// The typographic apostrophe the boards use; a straight one is a different
// glyph and the goldens would show it.
constexpr const char* kApos = "\xE2\x80\x99";

// THE FOUR SENTENCES, and all four end "Wi-Fi is off again." because that is
// true on every path -- which is what makes it worth saying once in one place.
//
// EVERY ONE WAS MEASURED AGAINST THE WRAP BOUNDARY IN BOTH DIRECTIONS before
// it was written here. The firmware's .rfnt faces measure ~3% wider than
// Chrome's, so a line that merely fits on the board wraps differently on
// glass, the centred panel grows, and every rule inside it lands out of
// register -- #76's defect, which cost 11.12% against 3.58%.
//
// AND THE FIGURES ARE MECHANICAL NOW, which is what falsified the claim this
// paragraph used to end with ("the shipped wording clears by at least 4% of
// the column both ways; see the boards"). Run by test_wifi_error_copy.cpp
// against the real ramp, the four shapes clear by 19px (BadPassword), 6px
// (NotFound), 40px (Incomplete) and 17px (ListFull) of next-word overflow on
// a 336px column -- so NotFound is at 1.8%, not 4%, and is the one to look at
// if this screen ever drifts. A hand measurement nobody can re-run is how a
// number outlives the thing it measured.
//
// THE FIRST DOES NOT LEAD WITH THE SSID where the other three do, and that is
// a trade forced by measurement rather than a preference: every SSID-first
// wording tried sat inside the floor in one direction or the other.
std::string sentence(JoinFailure why, const std::string& ssid) {
  const std::string quoted = std::string(kOpenQuote) + ssid + kCloseQuote;
  switch (why) {
    case JoinFailure::BadPassword:
      return "Wrong password for " + quoted + ". Wi-Fi is off again.";
    case JoinFailure::NotFound:
      return quoted + " didn" + kApos + "t answer. It may be out of range. Wi-Fi is off again.";
    case JoinFailure::ListFull:
      // THE ONE SHAPE THAT OPENS WITH A SUCCESS, and it has to: the reader just
      // watched a join finish, so a sentence that led with the refusal would
      // read as the password having been wrong. `joined` is the first word
      // after the name for that reason.
      //
      // THE CAP IS NAMED AND THE REMEDY IS GIVEN, because a refusal with no
      // remedy reads as a fault -- WallabagErrorNoNetwork's "Join one in
      // Settings first." is the same sentence doing the same job. The cap is
      // spelled `eight` rather than composed from kMaxSavedNetworks: this
      // string was MEASURED against #76's wrap floor at that width (17px of
      // next-word clearance, see test_wifi_error_copy.cpp), and a number the
      // sentence does not know at authoring time cannot be measured at all.
      // Changing kMaxSavedNetworks is therefore a copy change, which is what
      // that test asserts.
      return quoted +
             " joined, but the saved list is full at eight. Forget one, then join again. "
             "Wi-Fi is off again.";
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
  // ONE CAPTION FOR THE THREE RADIO FAILURES, as BookError's three shapes share
  // `CAN'T OPEN FILE`: the caption names the event and the sentence names the
  // cause.
  //
  // AND A DIFFERENT ONE FOR ListFull, BECAUSE THE JOIN DID NOT FAIL (#162). The
  // radio came up, the AP took the passphrase and the credential is proven --
  // captioning that `COULDN'T JOIN` is the false-claim shape the other three
  // exist to prevent, arriving from the other side. WallabagError already
  // varies its caption across shapes (`COULDN'T SIGN IN` / `COULDN'T CONNECT`),
  // so this is the vocabulary rather than a new one.
  vm_.caption = std::string("COULDN") + kApos +
                (why_ == JoinFailure::ListFull ? "T SAVE IT" : "T JOIN");
  vm_.message = sentence(why_, ssid_);
  vm_.offersEdit = (why_ == JoinFailure::BadPassword);
  if (vm_.offersEdit) vm_.actions.push_back("EDIT PASSWORD");
  // `TRY AGAIN` IS ABSENT ON ListFull, NOT INERT, and the argument is
  // WallabagErrorNoNetwork's verbatim: the cap does not change between two
  // presses of a slab, so a retry would join, be refused identically and land
  // back on this dialog -- a button that can only ever fail. The remedy is one
  // hold away on the hub under the veil, which is where the single slab and
  // Back both land, so there is nothing for a second slab to do either.
  if (why_ != JoinFailure::ListFull) {
    vm_.actions.push_back("TRY AGAIN");
    vm_.actions.push_back("CANCEL");
  } else {
    vm_.actions.push_back("OK");
  }
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
  //
  // AND THE ONE-SLAB SHAPE NAMES ITS SLAB AND EMPTIES THE MOVERS, which is
  // WallabagError's rule at its own one-slab shapes: with one slab `SELECT`
  // promises a choice and Up and Down have no second row to reach. The two
  // empty slots are 36px wide rather than zero (kHintEmptySlotW) -- measuring
  // them as nothing would draw the other two in the wrong places.
  //
  // READ OFF THE SLAB LIST, never off `why_`, for the reason actionsFor was
  // deleted: a second spelling of the count is free to drift from the count.
  vm_.hints = vm_.actions.size() == 1 ? std::array<std::string, 4>{"CANCEL", "OK", "", ""}
                                      : std::array<std::string, 4>{"CANCEL", "SELECT", "UP",
                                                                   "DOWN"};
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
        // `CANCEL` and ListFull's `OK` both land here, and that is the right
        // answer rather than a gap: Chosen names what the SHELL must do, and
        // both mean drop the attempt and pop to the hub. A fifth Chosen value
        // spelling the same three shell statements would be a label list
        // maintained twice.
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
