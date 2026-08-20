#include "doctest.h"
#include "reader/power.h"

using namespace reader;

TEST_CASE("no activity for the timeout asks for sleep") {
  IdleTimer t(60000);
  t.noteActivity(1000);
  CHECK(t.tick(60000) == PowerAction::None);
  CHECK(t.tick(61000) == PowerAction::Sleep);
}

TEST_CASE("activity resets the countdown") {
  IdleTimer t(60000);
  t.noteActivity(1000);
  CHECK(t.tick(50000) == PowerAction::None);
  t.noteActivity(50000);
  CHECK(t.tick(100000) == PowerAction::None);  // only 50 s since activity
  CHECK(t.tick(110000) == PowerAction::Sleep);
}

TEST_CASE("sleep is asked for once, not on every tick after the timeout") {
  IdleTimer t(1000);
  t.noteActivity(0);
  CHECK(t.tick(1000) == PowerAction::Sleep);
  CHECK(t.tick(1001) == PowerAction::None);
  CHECK(t.tick(99999) == PowerAction::None);
  // ...until the user does something, which re-arms it
  t.noteActivity(100000);
  CHECK(t.tick(101000) == PowerAction::Sleep);
}

TEST_CASE("a zero timeout never sleeps") {
  IdleTimer t(0);
  t.noteActivity(0);
  CHECK(t.tick(0xFFFFFFFFu) == PowerAction::None);
}

TEST_CASE("an idle period measured across a millisecond-clock wrap still sleeps") {
  IdleTimer t(1000);
  const uint32_t before = 0xFFFFFF00u;
  t.noteActivity(before);
  CHECK(t.tick(before + 500) == PowerAction::None);
  CHECK(t.tick(before + 1000) == PowerAction::Sleep);
}
