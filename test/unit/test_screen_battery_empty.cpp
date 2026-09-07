// design/BatteryEmpty.dc.html -- what the panel holds after a critical shutdown.
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/screen_battery_empty.h"
#include "reader/session_record.h"

using namespace reader;

TEST_CASE("BatteryEmpty carries the board's own copy") {
  BatteryEmptyScreen s;
  CHECK(s.id() == ScreenId::BatteryEmpty);
  CHECK(s.vm().title == "BATTERY EMPTY");
  CHECK(s.vm().note == "CHARGE \xC2\xB7 HOLD POWER TO WAKE");
  CHECK(s.vm().message.find("Your page is saved") != std::string::npos);
}

TEST_CASE("BatteryEmpty's badge states the HOLD, not just the charge") {
  // THE POINT OF THE COPY IS THE GESTURE. `CHARGE TO WAKE` promised a wake this
  // hardware cannot perform -- there is no charge-detect wake source, and on battery
  // the chip is fully powered down so no timer can fire one either -- and it omitted
  // the one thing the reader must actually do. setup() runs
  // requireHeldPowerButtonOrSleepAgain BEFORE requireChargeOrSleepAgain, so a tap does
  // nothing even on a charged pack. Asserted separately from the string above so a
  // future rewording cannot drop the gesture and still pass one of the two.
  BatteryEmptyScreen s;
  CHECK(s.vm().note.find("HOLD POWER") != std::string::npos);
  CHECK(s.vm().note.find("CHARGE") != std::string::npos);
  // A REAL U+00B7, in its own literal. A C++ hex escape is unbounded, so this project
  // has twice emitted a byte that is not the middle dot; the byte pair is what a
  // notdef box on glass would be traced back to.
  CHECK(s.vm().note.find("\xC2\xB7") != std::string::npos);
  // design/Sleep.dc.html's badge is what proves this one fits the 480px panel: the two
  // are the same length in CHARACTERS (the dot is two bytes), at the same role and the
  // same tracking, and that one already ships on the X4.
  CHECK(s.vm().note.size() == std::string("ASLEEP \xC2\xB7 HOLD POWER TO WAKE").size());
}

TEST_CASE("BatteryEmpty's prose names no connector") {
  // THE POINT OF THIS COPY IS WHAT IT DOES NOT SAY. It read `charge over USB-C` and was
  // reported from an X3, WHICH HAS NO USB-C PORT. One binary drives both models, they do
  // not share a connector, and nothing in the board profile names the socket -- so the
  // sentence cannot name one correctly and cannot be made conditional either. Asserted
  // as an absence rather than as a string so a future rewording that reintroduces any
  // connector fails, whichever one it picks.
  BatteryEmptyScreen s;
  const std::string& m = s.vm().message;
  // REQUIRE first: an empty or renamed message would satisfy every find() below
  // trivially, which is how a mutation-proof assertion turns into `0 == 0`.
  REQUIRE(m.find("Your page is saved") != std::string::npos);
  for (const char* connector : {"USB-C", "USB C", "USB-A", "micro-USB", "microUSB", "USB"}) {
    CHECK(m.find(connector) == std::string::npos);
  }
  // And it still says what to DO, which is the half that must survive the deletion.
  CHECK(m.find("connect a charger") != std::string::npos);
}

TEST_CASE("BatteryEmpty is Mono and takes no input") {
  BatteryEmptyScreen s;
  // Mono: there is no photograph on this screen and no body text, so three
  // waveforms would buy nothing on a pack that has none to spend.
  CHECK(s.fidelity() == Fidelity::Mono);
  // NOBODY IS LEFT TO PRESS ANYTHING. The shell paints this and calls deep sleep,
  // so every gesture answers none() -- including Back, which everywhere else means
  // "go up".
  for (Gesture what : {Gesture::Next, Gesture::Prev, Gesture::AltNext, Gesture::AltPrev,
                       Gesture::Activate, Gesture::Back}) {
    GestureEvent e;
    e.what = what;
    CHECK(s.onGesture(e).kind == Action::Kind::None);
  }
}

TEST_CASE("BatteryEmpty draws no hint bar") {
  // A bar is a contract about four buttons, and this screen's four do nothing.
  BatteryEmptyScreen s;
  for (const std::string& h : s.vm().hints) CHECK(h.empty());
}

TEST_CASE("the session record can name BatteryEmpty") {
  // THE TABLE MUST GROW WITH THE ENUM. session_record.cpp had THREE bounds spelled
  // `<= ScreenId::Peek`, so appending BookEnd made sessionWireName fall through to
  // kNames[0] and serialise the new screen as `home` -- a reader idle-sleeping on
  // it would have woken on Home, with no failing test and no log line.
  CHECK(std::string(sessionWireName(ScreenId::BatteryEmpty)) != "home");
  // The round trip. session_record.h exposes no `sessionScreenFromWire`: decoding
  // is decodeSessionStack over a whole record, so the name goes back through the
  // encoder that writes it.
  std::vector<StackEntry> in{{ScreenId::BatteryEmpty, -1}};
  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(encodeSessionStack(in).c_str(), out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].screen == ScreenId::BatteryEmpty);
}
