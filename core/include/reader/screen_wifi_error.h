#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"
#include "reader/wifi_radio.h"

namespace reader {

// design/WifiError.dc.html, design/WifiErrorNotFound.dc.html and
// design/WifiErrorFailed.dc.html -- ONE SCREEN, THREE COPY SHAPES.
//
// A join fails three distinguishable ways and ONE SENTENCE WOULD BE A LIE,
// which is BookError's argument and the reason it is three boards. Telling
// somebody their password was rejected by a router that is not there is the
// false-claim shape this project refuses for the battery gauge (-1, never 0%)
// and for a badge promising a wake charging cannot deliver.
//
// THE SLABS FOLLOW THE REASON, AND EDIT PASSWORD IS ABSENT RATHER THAN INERT
// on the two shapes where the password is not what went wrong. A slab that
// draws and does nothing is the works-only-sometimes trap this project has
// shipped twice; a slab that is not there teaches nothing because there is
// nothing to press. BookErrorMemory is the precedent, and its lesson is
// explicit: DO NOT MAKE IT INERT.
//
// It takes a JoinFailure rather than a reason code: wifiFailureFor does the
// mapping, in core/ beside its enum, so this screen cannot grow a second
// spelling of it.
class WifiErrorScreen : public FocusScreen {
 public:
  // What the reader pressed. Read by the shell after the pop, because every
  // one of them touches state this screen does not own -- the join attempt,
  // the radio, the stack.
  enum class Chosen { None, EditPassword, TryAgain, Cancel };

  WifiErrorScreen(std::string ssid, JoinFailure why);

  ScreenId id() const override { return ScreenId::WifiError; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const WifiErrorViewModel& vm() const { return vm_; }
  JoinFailure why() const { return why_; }
  Chosen chosen() const { return chosen_; }

 protected:
  void syncVm() override;

 private:
  // How many slabs this shape draws. THE ROW COUNT IS THE ONLY GATE: onGesture
  // is deliberately NOT also checked against `offersEdit`, because a second
  // condition is free to drift from the first, which is the class of bug Focus
  // was extracted to delete.
  static int actionsFor(JoinFailure why);

  std::string ssid_;
  JoinFailure why_;
  WifiErrorViewModel vm_;
  Chosen chosen_ = Chosen::None;
};

}  // namespace reader
