#include "reader/screen_sd_missing.h"

#include "reader/theme.h"

namespace reader {

SdMissingScreen::SdMissingScreen() {
  // The board's own copy, verbatim (design/SdMissing.dc.html). It lives here
  // rather than in the theme because it is content, and the theme is layout --
  // and rather than in a caller because there is nothing about a missing card
  // for a caller to have an opinion about.
  vm_.title = "NO SD CARD";
  vm_.message =
      "Books, articles, fonts, and reading progress live on the card. Insert one, then retry.";
  vm_.action = "RETRY";
  // Three empty slots, because three of the four buttons do nothing here: this
  // screen is the root, so there is nothing to go Back to, and there is nothing
  // to move a focus through. The board draws exactly that -- one hint, three
  // placeholders -- and the empty strings are what make it so, since a slot's
  // mark follows its label.
  vm_.hints = {"", "RETRY", "", ""};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

Action SdMissingScreen::onGesture(const GestureEvent& g) {
  // Confirm is the retry, and the ONLY thing this screen does. Up, Down and Back
  // are dead here on purpose: they have no hint, and a button that does something
  // the bar does not advertise is worse than one that does nothing.
  if (g.what == Gesture::Activate) return Action::retry();
  return Action::none();
}

void SdMissingScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderSdMissing(fb, fonts, vm_, plane);
}

}  // namespace reader
