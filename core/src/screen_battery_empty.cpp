#include "reader/screen_battery_empty.h"

#include "reader/theme.h"

namespace reader {

BatteryEmptyScreen::BatteryEmptyScreen() {
  // THE BOARD'S OWN COPY, transcribed once and here. "Your page is saved" is a
  // promise the shell's save keeps and markSleeping() redeems -- the screen must
  // not say it unless both run before it.
  vm_.title = "BATTERY EMPTY";
  // The board writes the dash as `&mdash;`, U+2014, which fontc.py's CODEPOINTS
  // carries -- a mapping onto a glyph the subset lacked would render as a notdef
  // box, which is worse than the wrong dash.
  vm_.message =
      "Your page is saved. The reader is shutting down \xE2\x80\x94 charge over USB-C to "
      "continue.";
  vm_.note = "CHARGE TO WAKE";
  // hints and holds stay empty -- see the header.
}

Action BatteryEmptyScreen::onGesture(const GestureEvent& g) {
  (void)g;
  return Action::none();
}

void BatteryEmptyScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                Plane plane) const {
  theme.renderBatteryEmpty(fb, fonts, vm_, plane);
}

}  // namespace reader
