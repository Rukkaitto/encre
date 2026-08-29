#include "doctest.h"
#include "reader/battery_tracker.h"

using namespace reader;

namespace {
// A reading the gauge answered. `charging` is deliberately a separate axis from
// `percent`: readStatus() reports each with its own Known flag, and a gauge can
// answer one and fail the other.
BatteryReading good(int percent, bool charging = false) {
  BatteryReading r;
  r.percentKnown = true;
  r.percent = percent;
  r.chargingKnown = true;
  r.charging = charging;
  return r;
}

// Every Known flag false: the I2C transaction did not complete.
BatteryReading failed() { return BatteryReading{}; }
}  // namespace

TEST_CASE("percent is -1 until a reading succeeds") {
  BatteryTracker t;
  CHECK(t.percent() == -1);
  // A FAILED reading must not move it off -1. readPercentage() answers 0 on
  // failure and percentageFromMillivolts maps a failed 0 mV to 0%, so a tracker
  // that trusted the value would put a flat battery on a healthy device.
  t.update(failed(), 0);
  CHECK(t.percent() == -1);
  t.update(good(64), 100);
  CHECK(t.percent() == 64);
}

TEST_CASE("a failed reading keeps the last good percentage") {
  BatteryTracker t;
  t.update(good(64), 0);
  t.update(failed(), 1000);
  CHECK(t.percent() == 64);
  t.update(good(63), 2000);
  CHECK(t.percent() == 63);
}

TEST_CASE("charging is false until a chargingKnown reading arrives, then last-known") {
  BatteryTracker t;
  CHECK(t.charging() == false);
  t.update(failed(), 0);
  CHECK(t.charging() == false);
  t.update(good(64, true), 1000);
  CHECK(t.charging() == true);
  // A failed reading leaves it, exactly as it leaves the percentage: the last
  // thing the gauge actually said beats a fabricated default.
  t.update(failed(), 2000);
  CHECK(t.charging() == true);
  t.update(good(64, false), 3000);
  CHECK(t.charging() == false);
}

TEST_CASE("a percent-only reading does not disturb charging, and vice versa") {
  BatteryTracker t;
  t.update(good(64, true), 0);
  BatteryReading percentOnly;
  percentOnly.percentKnown = true;
  percentOnly.percent = 50;
  t.update(percentOnly, 1000);
  CHECK(t.percent() == 50);
  CHECK(t.charging() == true);
}

TEST_CASE("a charging-only reading does not disturb percent") {
  BatteryTracker t;
  t.update(good(64, false), 0);
  BatteryReading chargingOnly;
  chargingOnly.chargingKnown = true;
  chargingOnly.charging = true;
  t.update(chargingOnly, 1000);
  CHECK(t.percent() == 64);
  CHECK(t.charging() == true);
}

TEST_CASE("the first reading seeds without asking for a repaint") {
  // Booting with the cable already in must not add a panel refresh to a boot that
  // is already painting Home. The edge needs a prior KNOWN not-charging state.
  BatteryTracker t;
  t.update(good(64, true), 0);
  CHECK(t.charging() == true);
  CHECK(t.takeRepaintRequest() == false);
}

TEST_CASE("not-charging to charging asks for exactly one repaint") {
  BatteryTracker t;
  t.update(good(64, false), 0);
  CHECK(t.takeRepaintRequest() == false);
  t.update(good(64, true), 1000);
  CHECK(t.takeRepaintRequest() == true);
  // Taken means taken: a second caller in the same state gets nothing.
  CHECK(t.takeRepaintRequest() == false);
  // And staying charging is not a new edge.
  t.update(good(65, true), 2000);
  CHECK(t.takeRepaintRequest() == false);
}

TEST_CASE("unplugging never asks for a repaint") {
  // Falling edges are free: the stale bolt is corrected by the next Home paint,
  // which is the same guarantee we would have had with no polling at all.
  BatteryTracker t;
  t.update(good(64, false), 0);
  t.update(good(64, true), 1000);
  CHECK(t.takeRepaintRequest() == true);
  t.update(good(64, false), 2000);
  CHECK(t.takeRepaintRequest() == false);
}

TEST_CASE("a dithering charge signal can never unlatch") {
  // THE CASE THE LATCH EXISTS FOR. On the X3 there is no charger IC, so charging
  // is a bare sign test on the gauge's Current() with no deadband -- and plugged
  // in at full charge is ~0 mA with a dithering sign. That is the state a device
  // spends all night in, and it must cost zero repaints.
  //
  // THE PATTERN MUST CONTAIN RUNS, AND A STRICT ALTERNATION WILL NOT DO. The dwell
  // is only ever consulted on a SECOND CONSECUTIVE not-charging sample -- the first
  // one just starts the clock -- so a flip on every sample never reaches that
  // branch, and a mutant with kUnlatchMs == 0 passes. Traced before this was
  // written: alternating gives 0 repaints at both 60 s and 0 ms, where the runs
  // below give 0 at 60 s and 2 at 0 ms. The runs are what make the constant
  // testable at all.
  //
  // Longest not-charging run here is 3 samples = 6 s, comfortably inside 60 s.
  BatteryTracker t;
  t.update(good(100, false), 0);
  t.update(good(100, true), 2000);
  REQUIRE(t.takeRepaintRequest() == true);
  const bool pattern[10] = {false, false, true, false, false,
                            false, true,  true, false, true};
  uint32_t now = 2000;
  for (int i = 0; i < 500; ++i) {  // ~17 minutes of 2 s polls
    now += 2000;
    t.update(good(100, pattern[i % 10]), now);
    CHECK(t.takeRepaintRequest() == false);
  }
}

TEST_CASE("a real unplug, held past the dwell, re-arms the latch") {
  BatteryTracker t;
  t.update(good(64, false), 0);
  t.update(good(64, true), 1000);
  REQUIRE(t.takeRepaintRequest() == true);
  t.update(good(64, false), 2000);
  // One millisecond short of the dwell: still latched.
  t.update(good(64, false), 2000 + BatteryTracker::kUnlatchMs - 1);
  t.update(good(64, true), 2000 + BatteryTracker::kUnlatchMs);
  CHECK(t.takeRepaintRequest() == false);

  BatteryTracker u;
  u.update(good(64, false), 0);
  u.update(good(64, true), 1000);
  REQUIRE(u.takeRepaintRequest() == true);
  u.update(good(64, false), 2000);
  u.update(good(64, false), 2000 + BatteryTracker::kUnlatchMs);
  u.update(good(64, true), 3000 + BatteryTracker::kUnlatchMs);
  CHECK(u.takeRepaintRequest() == true);
}

TEST_CASE("the session cap stops it asking after kMaxGrantsPerSession") {
  BatteryTracker t;
  uint32_t now = 0;
  int granted = 0;
  for (int cycle = 0; cycle < BatteryTracker::kMaxGrantsPerSession + 2; ++cycle) {
    t.update(good(64, false), now);
    now += BatteryTracker::kUnlatchMs + 1;
    t.update(good(64, false), now);
    now += 1000;
    t.update(good(64, true), now);
    if (t.takeRepaintRequest()) ++granted;
    now += 1000;
  }
  CHECK(granted == BatteryTracker::kMaxGrantsPerSession);
}

TEST_CASE("a failed reading neither arms nor fires the latch") {
  // chargingKnown false is "the gauge did not answer", not "not charging".
  BatteryTracker t;
  t.update(good(64, false), 0);
  t.update(failed(), 1000);
  CHECK(t.takeRepaintRequest() == false);
  t.update(good(64, true), 2000);
  CHECK(t.takeRepaintRequest() == true);
}
