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

TEST_CASE("an unplug asks for nothing until the dwell confirms it") {
  // The falling edge ITSELF is free -- a bare "charging went false" must not
  // spend a repaint, because on the X3 that edge fires on every tick of the
  // gauge's dithering sign at full charge. See "a confirmed unplug asks for
  // exactly one repaint" below for what an unplug HELD past the dwell does:
  // this test is the immediate sample only, one second after the plug-in.
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

TEST_CASE("a confirmed unplug asks for exactly one repaint") {
  // THE DEFECT THIS GUARDS AGAINST, reported from an X3: the bolt appears
  // within ~2 s of plugging in (correct) and then stays on glass indefinitely
  // after unplugging, because nothing repaints Home once charging_ goes false
  // and the user is sitting on Home pressing nothing. The design's original
  // rule -- unplugging never spends a refresh -- assumed a button press would
  // correct it; sitting still, nothing does.
  //
  // The fix rides the SAME dwell that already exists to tell a real unplug
  // from the gauge's zero-current dither, so it needs no new constant and
  // inherits the same by-construction argument: a signal that cannot hold
  // kUnlatchMs of unbroken not-charging can never reach this branch at all.
  BatteryTracker t;
  t.update(good(64, false), 0);
  t.update(good(64, true), 1000);
  REQUIRE(t.takeRepaintRequest() == true);

  // The dwell clock starts at the FIRST not-charging sample after the plug,
  // not at the plug itself -- that one sample only arms sawNotCharging_ and
  // stamps notChargingSinceMs_; the dwell is checked only from the SECOND
  // not-charging sample onward. It must not grant anything on its own.
  const uint32_t unplugAt = 2000;
  t.update(good(64, false), unplugAt);
  CHECK(t.takeRepaintRequest() == false);

  // Continuous not-charging, sampled every 2 s like the shell's real poll,
  // right up to the dwell: nothing granted yet.
  uint32_t now = unplugAt;
  while (now + 2000 < unplugAt + BatteryTracker::kUnlatchMs) {
    now += 2000;
    t.update(good(64, false), now);
    CHECK(t.takeRepaintRequest() == false);
  }

  // The sample that completes kUnlatchMs of unbroken not-charging, measured
  // from that first not-charging sample, grants exactly one repaint -- the
  // clearing this feature exists for.
  now = unplugAt + BatteryTracker::kUnlatchMs;
  t.update(good(64, false), now);
  CHECK(t.takeRepaintRequest() == true);

  // Taken means taken, and continuing to report not-charging asks for nothing
  // further: the latch is already clear, so there is no second dwell to cross
  // and nothing left to confirm.
  for (int i = 0; i < 20; ++i) {
    now += 2000;
    t.update(good(64, false), now);
    CHECK(t.takeRepaintRequest() == false);
  }
}

TEST_CASE("the clearing repaint respects the session cap") {
  // The clearing fires through the SAME grants_ < kMaxGrantsPerSession gate as
  // the rising edge -- one budget, whichever edge spends it -- so a full
  // plug/unplug cycle can now cost up to TWO grants instead of one: one for
  // the plug, one for the confirmed unplug. Reach the cap with a mix of both
  // and confirm neither edge asks for anything more once it is spent.
  BatteryTracker t;
  uint32_t now = 0;

  t.update(good(64, false), now);
  now += 1000;
  t.update(good(64, true), now);  // grant 1: the plug
  REQUIRE(t.takeRepaintRequest() == true);

  now += BatteryTracker::kUnlatchMs;
  t.update(good(64, false), now);  // dwell clock starts
  now += BatteryTracker::kUnlatchMs;
  t.update(good(64, false), now);  // grant 2: the confirmed unplug
  REQUIRE(t.takeRepaintRequest() == true);

  now += 1000;
  t.update(good(64, true), now);  // grant 3: the plug -- cap now fully spent
  REQUIRE(t.takeRepaintRequest() == true);

  now += BatteryTracker::kUnlatchMs;
  t.update(good(64, false), now);  // dwell clock starts again
  now += BatteryTracker::kUnlatchMs;
  t.update(good(64, false), now);  // would be grant 4: refused, cap already hit
  CHECK(t.takeRepaintRequest() == false);

  // And the cap holds for a further plug too, not just the clearing -- it is
  // one shared budget, not one counter per edge.
  now += 1000;
  t.update(good(64, true), now);
  CHECK(t.takeRepaintRequest() == false);
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
  // UPDATED FOR THE CLEARING REPAINT (2026-08-29): a full plug/unplug cycle now
  // spends up to two grants -- one for the plug, one for the confirmed unplug
  // -- from the SAME counter, not one each. So a loop of full cycles hits the
  // cap after fewer cycles than before, and the request has to be taken after
  // BOTH updates that can fire one (the dwell-crossing not-charging sample as
  // well as the following plug) or a grant spent by the clearing would go
  // unobserved, exactly as it did before this test was fixed: it used to read
  // `granted == 3` from one take() per cycle and started failing with `2 == 3`
  // the moment the clearing began spending grants of its own.
  BatteryTracker t;
  uint32_t now = 0;
  int granted = 0;
  for (int cycle = 0; cycle < BatteryTracker::kMaxGrantsPerSession + 2; ++cycle) {
    t.update(good(64, false), now);
    now += BatteryTracker::kUnlatchMs + 1;
    t.update(good(64, false), now);
    if (t.takeRepaintRequest()) ++granted;
    now += 1000;
    t.update(good(64, true), now);
    if (t.takeRepaintRequest()) ++granted;
    now += 1000;
  }
  // Whatever the split between the two edges, the total never exceeds the cap.
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

TEST_CASE("a boot onto the dither's low side costs at most one grant") {
  // The first-reading seed only covers a first sample that reads CHARGING. A
  // boot that lands on the not-charging side of a dithering signal is
  // indistinguishable from a real plug-in, and must not be closed off: the
  // "not-charging to charging" case above is the same shape and is the feature's
  // whole point. The session cap is what bounds it.
  BatteryTracker t;
  t.update(good(100, false), 0);
  t.update(good(100, true), 2000);
  CHECK(t.takeRepaintRequest() == true);
  int extra = 0;
  uint32_t now = 2000;
  const bool pattern[10] = {false, false, true, false, false,
                            false, true,  true, false, true};
  for (int i = 0; i < 200; ++i) {
    now += 2000;
    t.update(good(100, pattern[i % 10]), now);
    if (t.takeRepaintRequest()) ++extra;
  }
  CHECK(extra == 0);
}
