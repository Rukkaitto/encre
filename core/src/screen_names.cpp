#include "reader/screen_names.h"

#include "reader/theme.h"

namespace reader {

NamesScreen::NamesScreen() {
  vm_.title = "NAMES";
  // THE COPY IS design/NamesEmpty.dc.html's, WORD FOR WORD. It says when the list
  // will fill rather than that it is empty: "no names yet" alone invites the reader
  // to conclude the feature is broken or that this book is not supported, where
  // naming the mechanism turns an empty screen into an explained one. Same reasoning
  // as HomeEmpty's paragraph.
  vm_.emptyTitle = "NO NAMES YET";
  vm_.emptyBody =
      "Names appear as you read. Keep going and the people and places this book uses "
      "will collect here.";
  // BACK ONLY, and the other three slots are the boards' 36px dead spacers -- which
  // `buildHints` applies for us, so this is four entries rather than one. There is
  // nothing to press: the only thing that fills this list is reading. Names.dc.html's
  // `MENTIONS` arrives with the rows it promises.
  vm_.hints = {"BACK", "", "", ""};
  vm_.holds = {false, false, false, false};
}

Action NamesScreen::onGesture(const GestureEvent& g) {
  // ONE BINDING, and every other button is dead rather than silently ignored: the
  // hint bar says so, which is the rule that keeps a dead button from reading as a
  // broken device.
  if (g.what == Gesture::Back) return Action::pop();
  return Action::none();
}

void NamesScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                         Plane plane) const {
  theme.renderNames(fb, fonts, vm_, plane);
}

}  // namespace reader
