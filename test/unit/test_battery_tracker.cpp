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

// --- The level ladder ----------------------------------------------------------

TEST_CASE("the level is Normal until a reading says otherwise") {
  BatteryTracker t;
  // NEVER READ is Normal, not Critical. A device that shut itself down because the
  // gauge had not answered yet would be unusable, and percent() already answers -1
  // for the same state.
  CHECK(t.level() == BatteryLevel::Normal);
  t.update(good(50), 0);
  CHECK(t.level() == BatteryLevel::Normal);
}

TEST_CASE("Low is entered at kLowPercent and needs no dwell") {
  BatteryTracker t;
  t.update(good(11), 0);
  CHECK(t.level() == BatteryLevel::Normal);
  // No dwell: the cost of being wrong is one banner, not a shutdown.
  t.update(good(BatteryTracker::kLowPercent), 1000);
  CHECK(t.level() == BatteryLevel::Low);
}

TEST_CASE("Low is left again when the battery comes back up") {
  BatteryTracker t;
  t.update(good(5), 0);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(40), 1000);
  CHECK(t.level() == BatteryLevel::Normal);
}

TEST_CASE("Critical requires the dwell, continuously") {
  BatteryTracker t;
  t.update(good(BatteryTracker::kCriticalPercent), 0);
  // Below the threshold but not yet held: the level is Low, not Critical. A single
  // sagging reading -- and an e-ink refresh is the heaviest load this device draws
  // -- must not be able to shut it down.
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(2), BatteryTracker::kCriticalDwellMs - 1);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(2), BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("one reading above the threshold restarts the dwell") {
  BatteryTracker t;
  t.update(good(1), 0);
  t.update(good(1), BatteryTracker::kCriticalDwellMs - 1);
  CHECK(t.level() == BatteryLevel::Low);
  // The recovery. The dwell is measured from the FIRST reading in an UNBROKEN run,
  // exactly as kUnlatchMs's is, so this one resets it.
  t.update(good(20), BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Normal);
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1);
  // Held for the dwell measured from HERE, not from 0.
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1 + BatteryTracker::kCriticalDwellMs - 1);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1 + BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("charging suppresses Critical but not Low") {
  BatteryTracker t;
  for (uint32_t ms = 0; ms <= BatteryTracker::kCriticalDwellMs * 2;
       ms += BatteryTracker::kCriticalDwellMs / 4)
    t.update(good(1, /*charging=*/true), ms);
  // A device on the cable must not shut down, however long it has been flat. The
  // banner still shows: the battery IS low, and saying so is true.
  CHECK(t.level() == BatteryLevel::Low);
  // Unplugged, the dwell starts now rather than being satisfied by the time spent
  // charging -- a run that was suppressed was not a run.
  t.update(good(1, /*charging=*/false), BatteryTracker::kCriticalDwellMs * 2 + 1);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1, /*charging=*/false),
           BatteryTracker::kCriticalDwellMs * 2 + 1 + BatteryTracker::kCriticalDwellMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("a failed reading holds the level where it was") {
  BatteryTracker t;
  t.update(good(1), 0);
  CHECK(t.level() == BatteryLevel::Low);
  // A transient I2C miss is not a fact about the battery. "Flat" and "did not
  // answer" are different claims -- the same rule percent()'s -1 already keeps --
  // and a dwell satisfied by silence would shut the device down on a bus glitch.
  t.update(failed(), BatteryTracker::kCriticalDwellMs * 2);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(50), BatteryTracker::kCriticalDwellMs * 2 + 1);
  CHECK(t.level() == BatteryLevel::Normal);
}

TEST_CASE("a failed reading cannot complete a dwell that was already running") {
  BatteryTracker t;
  t.update(good(1), 0);
  t.update(failed(), BatteryTracker::kCriticalDwellMs);
  // The clock ran, but nothing confirmed the battery is still flat.
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1), BatteryTracker::kCriticalDwellMs + 1);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("the ladder's thresholds are ordered and X4-reachable") {
  // The X4's ADC reports 10% notches (LIION_NOTCH_MV), so a threshold it cannot
  // express is a threshold that never fires there. Asserted rather than trusted to
  // a comment, because changing one of these numbers is exactly the edit that would
  // silently disarm the feature on half the fleet.
  static_assert(BatteryTracker::kCriticalPercent < BatteryTracker::kLowPercent, "");
  static_assert(BatteryTracker::kLowPercent < BatteryTracker::kResumePercent, "");
  // Critical must be reachable as the notch `0`.
  static_assert(BatteryTracker::kCriticalPercent < 10, "");
  // Low must be reachable as the notch `10`.
  static_assert(BatteryTracker::kLowPercent % 10 == 0, "");
  // Resume must need the notch `20`, which is a DIFFERENT voltage from the one
  // Critical fires at -- 3.71 V against 3.565 V. Without that gap the shutdown edge
  // and the resume edge are the same midpoint and a device on the cable flaps.
  static_assert(BatteryTracker::kResumePercent > 10, "");
  static_assert(BatteryTracker::kResumePercent <= 20, "");
  CHECK(true);
}

// --- The cadence (#96) --------------------------------------------------------
//
// WHAT THESE DEFEND IS THE WORD `CONTINUOUS`. Both dwells above are timestamp
// arithmetic over a sequence of samples, so a longer interval between samples does
// not lengthen a dwell -- it thins it. At a cadence equal to the dwell, "60 s of
// unbroken not-charging" becomes "two samples 60 s apart", which is a claim about
// two instants and not about a minute. So the cadence is not a free parameter: it
// is bounded by the dwells, and these cases are what say so.
//
// The interval also has to be tested as a CLOSED LOOP -- the tracker choosing the
// cadence at which it is next fed -- because that is what the shell does, and the
// property the design rests on (the critical dwell is never sampled slowly) is a
// fact about the loop rather than about either half of it.

TEST_CASE("the slow cadence cannot span either dwell in one gap") {
  // THE BOUND ON kPollSlowMs, and the reason #96's "minutes" is not taken: 60 s is
  // where minutes start and kUnlatchMs IS 60 s, so a minute-long interval makes the
  // longest dwell here satisfiable by a single gap between two samples.
  static_assert(BatteryTracker::kPollSlowMs < BatteryTracker::kUnlatchMs, "");
  static_assert(BatteryTracker::kPollFastMs < BatteryTracker::kPollSlowMs, "");
  // And the fast cadence must put more than one sample inside the critical dwell,
  // for the same reason one reading is not a flat pack.
  static_assert(BatteryTracker::kPollFastMs * 2 <= BatteryTracker::kCriticalDwellMs, "");
  CHECK(true);
}

TEST_CASE("slow is returned only while Normal with no band repaint to make") {
  BatteryTracker t;
  // A tracker that has never read anything is Normal, which is the boot state --
  // and the boot paints Home, so the shell's first reading arrives with the band
  // reachable and the fast cadence chosen.
  CHECK(t.pollIntervalMs(true) == BatteryTracker::kPollFastMs);
  CHECK(t.pollIntervalMs(false) == BatteryTracker::kPollSlowMs);

  t.update(good(64), 0);
  REQUIRE(t.level() == BatteryLevel::Normal);
  CHECK(t.pollIntervalMs(true) == BatteryTracker::kPollFastMs);
  CHECK(t.pollIntervalMs(false) == BatteryTracker::kPollSlowMs);
}

TEST_CASE("every rung below Normal polls at the cadence that shipped") {
  // The banner, the critical dwell and the shutdown all sit downstream of the Low
  // crossing, so none of them is slowed by this change -- whatever the caller says
  // about the band.
  BatteryTracker t;
  t.update(good(BatteryTracker::kLowPercent), 0);
  REQUIRE(t.level() == BatteryLevel::Low);
  CHECK(t.pollIntervalMs(false) == BatteryTracker::kPollFastMs);
  CHECK(t.pollIntervalMs(true) == BatteryTracker::kPollFastMs);

  t.update(good(1), BatteryTracker::kPollFastMs);
  t.update(good(1), BatteryTracker::kPollFastMs + BatteryTracker::kCriticalDwellMs);
  REQUIRE(t.level() == BatteryLevel::Critical);
  CHECK(t.pollIntervalMs(false) == BatteryTracker::kPollFastMs);
  CHECK(t.pollIntervalMs(true) == BatteryTracker::kPollFastMs);
}

TEST_CASE("the critical dwell is never sampled at the slow cadence") {
  // THE CLOSED LOOP, and the structural claim it proves: the critical run can only
  // be armed by a reading that has already set level_ = Low, so the interval chosen
  // AFTER it is fast by construction. Driven with the band unreachable throughout --
  // a reader in a book on an X4, the slowest state there is -- and from a pack that
  // is flat from the first reading, which is the worst case for the arming step.
  BatteryTracker t;
  uint32_t now = 0;
  int steps = 0;
  uint32_t armedAt = 0;
  bool armed = false;
  int slowGapsAfterArming = 0;
  while (t.level() != BatteryLevel::Critical) {
    REQUIRE(steps < 100);  // the loop must terminate, not merely not fail
    const uint32_t interval = t.pollIntervalMs(/*bandRepaintPossible=*/false);
    if (armed && interval != BatteryTracker::kPollFastMs) ++slowGapsAfterArming;
    now += interval;
    t.update(good(BatteryTracker::kCriticalPercent), now);
    if (!armed && t.level() == BatteryLevel::Low) {
      armed = true;
      armedAt = now;
    }
    ++steps;
  }
  // NOT ONE SLOW GAP once the run is armed. This is the assertion the whole design
  // rests on; if it ever fails, kCriticalDwellMs's "continuous" is being measured
  // across intervals longer than the dwell itself (kPollSlowMs > kCriticalDwellMs).
  CHECK(slowGapsAfterArming == 0);
  // And the dwell itself is unchanged -- Critical lands exactly kCriticalDwellMs
  // after the reading that armed the run, not sooner and not a slow interval later.
  CHECK(now - armedAt == BatteryTracker::kCriticalDwellMs);
  // The whole walk costs one slow interval (noticing Low) plus the dwell. That one
  // interval is the entire latency #96 buys, and it is absorbed by the pack: the Low
  // band runs from kLowPercent to kCriticalPercent, and on an X4 it is a whole 10%
  // notch -- hours of discharge, which 30 s cannot skip.
  CHECK(now == BatteryTracker::kPollSlowMs + BatteryTracker::kCriticalDwellMs);
}

TEST_CASE("a flat pack still reaches Critical if the cadence never speeds up") {
  // FAIL-SAFE, NOT FAIL-DEPENDENT. The adaptive cadence is an optimisation, so a
  // caller that ignored pollIntervalMs and polled slowly for ever must still shut
  // the device down -- later, never not at all. Without this the safety mechanism
  // would rest on the shell obeying an interval, which is the caller-list shape this
  // project turns into a function rather than a rule someone remembers.
  BatteryTracker t;
  t.update(good(1), 0);
  CHECK(t.level() == BatteryLevel::Low);
  t.update(good(1), BatteryTracker::kPollSlowMs);
  CHECK(t.level() == BatteryLevel::Critical);
}

TEST_CASE("one sagging reading cannot shut the device down at either cadence") {
  // kCriticalDwellMs's own reason: a panel refresh is the heaviest load this device
  // draws and the 0% anchor leaves headroom for that sag, so ONE low reading is not
  // a flat pack. Asserted at BOTH cadences, because a cadence coarser than the dwell
  // would let the sample after a sag complete a dwell the sag itself started.
  for (const uint32_t cadence : {BatteryTracker::kPollFastMs, BatteryTracker::kPollSlowMs}) {
    BatteryTracker t;
    t.update(good(64), 0);
    // The sag: one reading at the bottom of the ladder, taken under a waveform.
    t.update(good(1), cadence);
    CHECK(t.level() == BatteryLevel::Low);
    // And the pack was fine all along.
    t.update(good(64), cadence * 2);
    CHECK(t.level() == BatteryLevel::Normal);
    t.update(good(64), cadence * 3);
    CHECK(t.level() == BatteryLevel::Normal);
  }
}

TEST_CASE("the unlatch dwell needs more than one slow gap") {
  // A CONFIRMED UNPLUG AT THE SLOW CADENCE, which is reachable: the shell chooses
  // fast whenever the repaint could fire, but the latch's STATE keeps evolving off
  // Home, where it cannot. Two gaps of kPollSlowMs, not one -- the first stamps the
  // run and the second is still inside the dwell.
  BatteryTracker t;
  uint32_t now = 0;
  t.update(good(64, false), now);  // seed: not charging
  now += BatteryTracker::kPollSlowMs;
  t.update(good(64, true), now);  // plug in -> latch
  REQUIRE(t.takeRepaintRequest());
  now += BatteryTracker::kPollSlowMs;
  t.update(good(64, false), now);  // unplug seen; stamps the run
  CHECK_FALSE(t.takeRepaintRequest());
  now += BatteryTracker::kPollSlowMs;  // one slow gap: still short of 60 s
  t.update(good(64, false), now);
  CHECK_FALSE(t.takeRepaintRequest());
  now += BatteryTracker::kPollSlowMs;  // two gaps: 60 s reached
  t.update(good(64, false), now);
  CHECK(t.takeRepaintRequest());
}

TEST_CASE("a dithering charge signal cannot unlatch at the slow cadence either") {
  // The existing case for this runs at 2 s. At 30 s a PAIR of samples could
  // legitimately both land on the not-charging side of a signal that flips every few
  // seconds -- what stops the flicker there is that the run is BROKEN by any
  // charging sample, and a dither produces those. Asserted rather than argued,
  // because it is the one guarantee the slow cadence weakens by construction.
  BatteryTracker t;
  uint32_t now = 0;
  t.update(good(100, false), now);
  now += BatteryTracker::kPollSlowMs;
  t.update(good(100, true), now);
  REQUIRE(t.takeRepaintRequest());
  // Alternating at the slow cadence, well past kUnlatchMs in wall-clock terms.
  for (int i = 0; i < 20; ++i) {
    now += BatteryTracker::kPollSlowMs;
    t.update(good(100, i % 2 == 0), now);
    CHECK_FALSE(t.takeRepaintRequest());
  }
  CHECK(now > BatteryTracker::kUnlatchMs);
}
