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
