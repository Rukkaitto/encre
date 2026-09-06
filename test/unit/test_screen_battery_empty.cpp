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
  CHECK(s.vm().note == "CHARGE TO WAKE");
  CHECK(s.vm().message.find("Your page is saved") != std::string::npos);
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
