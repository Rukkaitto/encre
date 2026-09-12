#include "reader/screen_battery_empty.h"

#include <string>

#include "reader/theme.h"

namespace reader {

namespace {

// THE MIDDLE DOT, SPACES INCLUDED, AND THE SEPARATE LITERAL IS LOAD-BEARING. A C++
// hex escape is UNBOUNDED, so a "\xC2\xB7H..." written as one literal would parse
// `\xB7H` -- clang rejects it and the ESP32's GCC ACCEPTS it, emitting a byte that is
// not U+00B7. This project has paid for that exact shape twice. Its own literal ends
// the escape whatever follows.
//
// AND IT CANNOT SHARE screens.cpp's `kDot`, which is the obvious objection: that one
// is `const char* const` in an ANONYMOUS NAMESPACE inside a .cpp, so it is reachable
// from nothing. Nor is it the same string -- `kDot` is " \xC2\xB7 " with its spaces
// baked in, where screen_book_end.cpp's kMiddot is the bare two bytes and
// screen_library.cpp's carries spaces again. Every site spells it locally
// (screens.cpp, screen_library.cpp, screen_book_end.cpp, and inline in
// theme_quiet.cpp and screen_peek.cpp) and deliberately so: those comments record it
// as a punctuation choice each board makes rather than a constant, and the day one
// board wants an en dash a shared one would have to be un-shared. This file follows
// screen_book_end.cpp -- bare bytes, spaces supplied at the join, so the surrounding
// literals are visible where the string is read.
const char* const kMiddot = "\xC2\xB7";

}  // namespace

BatteryEmptyScreen::BatteryEmptyScreen() {
  // THE BOARD'S OWN COPY, transcribed once and here. "Your page is saved" is a
  // promise the shell's save keeps and markSleeping() redeems -- the screen must
  // not say it unless both run before it.
  vm_.title = "BATTERY EMPTY";
  // THREE SENTENCES, AND THE DASH BETWEEN THE LAST TWO IS GONE. This read
  // `shutting down \xE2\x80\x94 connect`, and a dash standing in for a full stop is the
  // one punctuation habit the rest of this device's copy does not have: every other
  // prompt on the glass states its facts as separate sentences (`The file leaves the
  // SD card. Your progress and bookmarks are kept...`, `Books, articles, fonts, and
  // reading progress live on the card. Insert one, then retry.`), so this one line
  // was the outlier rather than the house style.
  //
  // MEASURED AND NOT ASSUMED, on test_book_error_copy.cpp's own instrument: the
  // paragraph still wraps to FOUR lines at the same four breaks, and the tightest
  // next-word overflow is UNCHANGED at 33px. So the column's height, and therefore
  // its centring, did not move -- what changed is the glyphs on two of the four
  // lines. See `BatteryEmpty's copy clears the wrap boundary` below.
  //
  // THE CONNECTOR IS DELIBERATELY UNNAMED. This said `charge over USB-C` and was
  // reported from an X3, WHICH HAS NO USB-C PORT -- so the sentence was false on the
  // model it was read on. One binary drives both the X3 and the X4, they do not share
  // a connector, and nothing in the board profile names the socket (there is no such
  // field), so the copy can neither name one correctly nor be made conditional.
  // `Connect a charger` is true on both, and is 17 characters exactly as
  // `charge over USB-C` was, so the paragraph wraps identically -- four lines at the
  // same break positions, verified against the board in Chrome at both geometries
  // rather than assumed. Same rule as the badge below: a false claim is worse than an
  // absent one.
  vm_.message = "Your page is saved. The reader is shutting down. Connect a charger to continue.";
  // THE BADGE NAMES TWO STEPS BECAUSE THE WAKE TAKES TWO. It said `CHARGE TO WAKE`,
  // and both halves of that were wrong -- reported from an X3 as confusing, which it
  // was. Charging cannot wake this hardware: there is no charge-detect wake source
  // (`usbDetect` is a field declaration in the SDK's BoardConfig.h that nothing in the
  // SDK reads, and on the X3 the pin the Xteink profile names for it is the fuel
  // gauge's own I2C SDA), and no timer wake either, because on battery the sleep leaves
  // the chip FULLY POWERED DOWN -- which is why a resume reports ESP_RST_POWERON rather
  // than ESP_RST_DEEPSLEEP. And the hold came first: setup() runs
  // requireHeldPowerButtonOrSleepAgain BEFORE requireChargeOrSleepAgain, so a tap does
  // nothing even on a charged pack. A badge that promises what the hardware cannot do
  // is the same defect class this project refuses for an unread gauge (`-1`, never
  // `0%`).
  //
  // The width is proven by a sibling rather than by a fresh measurement: this is
  // character-for-character as long as design/Sleep.dc.html's
  // `ASLEEP \xC2\xB7 HOLD POWER TO WAKE`, which already ships on the narrower X4.
  vm_.note = std::string("CHARGE ") + kMiddot + " HOLD POWER TO WAKE";
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
