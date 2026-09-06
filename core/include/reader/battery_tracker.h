#pragma once

#include <cstdint>

namespace reader {

// ONE READING OF THE BATTERY, as BatteryMonitor::readStatus() reports it.
//
// The per-field `Known` flags are the load-bearing part and the reason this is not
// two ints. An I2C gauge read can fail transiently, and BatteryMonitor's unchecked
// accessors answer a FAILURE with 0 -- readPercentage() returns 0, and
// percentageFromMillivolts maps a failed 0 mV to 0% rather than 100%, deliberately.
// So "0%" and "I could not read it" are the same value out of that API and must not
// be the same value in here: a tracker that conflated them would put a flat battery
// on the panel of a device that is fine.
struct BatteryReading {
  bool percentKnown = false;
  int percent = 0;  // 0..100 when percentKnown
  bool chargingKnown = false;
  bool charging = false;
};

// WHICH RUNG OF THE SAFETY LADDER THE PACK IS ON.
//
// Not a percentage the caller thresholds itself: a threshold spelled at the call
// site is a threshold that can be spelled differently at the next one, and this
// project has shipped a dead button twice from exactly that shape. The shell asks
// which rung; the numbers live here with their derivation.
enum class BatteryLevel : uint8_t { Normal, Low, Critical };

// WHAT THE BAND SHOULD SAY, AND WHETHER A CHANGE OF CHARGE STATE IS WORTH A REPAINT.
//
// Both edges can be: a plug-in repaints on a rising edge, and an unplug repaints
// once the dwell below has confirmed it -- see "THE CLEARING REPAINT" in update().
// The original design fired on the rising edge only and left the falling edge to
// the next Home paint, on the assumption that a button press would supply one.
// Reported from an X3 (2026-08-29): sitting on Home with nothing pressed, nothing
// does, and the bolt stays on glass indefinitely after the cable comes out.
//
// In core/ rather than shell/ for the reason ProgressSaveGate is: this is a latch
// with a dwell timer and a session cap, `shell/` has no test harness, and five bugs
// have hidden there. It keeps no clock of its own -- `nowMs` is passed in, because
// core/ has no clock and must not acquire one.
class BatteryTracker {
 public:
  // -1 = never read. Chosen to match homeVmForCard()'s LIBRARY count, which is -1
  // for "the directory could not be read" and draws a blank row rather than a 0:
  // "no books" and "could not look" are different claims, and so are "flat" and
  // "did not answer".
  static constexpr int kUnknownPercent = -1;

  // THE ANTI-FLAP CONSTANT, and it works by construction rather than by tuning.
  //
  // On the X3 there is no charger IC (chargerAddr == 0), so BatteryMonitor answers
  // isCharging() with a bare sign test on the BQ27220's Current() register and no
  // deadband. Charging reads clearly positive and discharging clearly negative --
  // but PLUGGED IN AND FULL is about 0 mA with a dithering sign, and that is the
  // state a device spends all night in. Without this, an edge-triggered repaint
  // would flicker the panel until morning, burning the battery it reports on.
  //
  // Requiring the dwell to be CONTINUOUS is what makes a dither harmless: a
  // signal that flips every few seconds never accumulates a whole minute of
  // unbroken not-charging, so it never unlatches, so it never re-fires. A signal
  // that does hold for a minute and then flips is a real state change.
  static constexpr uint32_t kUnlatchMs = 60u * 1000u;

  // A backstop, in ProgressSaveGate's idiom, for the same reason it has one: it
  // turns a hazard that cannot be characterised without a bench into a bounded
  // one. Three extra panel refreshes per awake session, worst case, and then this
  // mechanism goes quiet and the mark simply waits for the next Home paint --
  // which is exactly the behaviour of not polling at all.
  //
  // ONE SHARED BUDGET FOR BOTH EDGES, NOT ONE EACH. The clearing repaint spends
  // from the same counter as the rising edge, so a full plug/unplug cycle can
  // now cost two grants instead of one -- accepted, because this was already a
  // backstop against a hardware quirk rather than a promise of exactly one
  // refresh per cycle.
  static constexpr int kMaxGrantsPerSession = 3;

  // --- The ladder ---------------------------------------------------------------
  //
  // THE X4 REPORTS 10% NOTCHES, which is what sets all three of these. Its ADC path
  // walks LIION_NOTCH_MV[11] and returns a multiple of ten, so a threshold that is
  // not expressible on that curve is one that never fires on half the fleet. The
  // board's `BATTERY LOW - 5%` is the value DISPLAYED, never the trigger.

  // The X4's lowest non-zero notch, ~3.68 V. Anything lower is unreachable there
  // until the pack is already at 0.
  static constexpr int kLowPercent = 10;

  // X4-reachable only as the notch `0`, which is <=3.565 V -- the midpoint of the
  // 3.45 V and 3.68 V anchors. The 0% anchor is deliberately above the cell's
  // protection cut-off and leaves headroom for the sag under an e-ink refresh, so
  // this is minutes of runtime rather than the cliff. Read literally on an X3.
  static constexpr int kCriticalPercent = 3;

  // THE HYSTERESIS, AND IT IS THE LOAD-BEARING NUMBER. The resume gate in setup()
  // refuses to wake below this. It must require the X4's *20%* notch, ~3.71 V,
  // because anything the X4 can satisfy at the 0/10 boundary puts the shutdown edge
  // and the resume edge at the SAME 3.565 V midpoint -- and a device on the cable
  // then shuts down, charges for a minute, wakes, discharges and shuts down again.
  // 145 mV between the two edges is what makes them different voltages.
  static constexpr int kResumePercent = 15;

  // CONTINUOUS, in kUnlatchMs's idiom and for its reason. A panel refresh is the
  // heaviest load this device draws and the SDK's own notch table says the 0%
  // anchor leaves headroom for that sag -- so one low reading is not a flat pack.
  // The poll already runs only in the shell's `quiet` window, which excludes a
  // sample taken mid-waveform; this is the belt to that braces.
  static constexpr uint32_t kCriticalDwellMs = 10u * 1000u;

  BatteryLevel level() const { return level_; }

  void update(const BatteryReading& r, uint32_t nowMs) {
    if (r.percentKnown) {
      percent_ = r.percent;
      havePercent_ = true;
    }
    if (r.chargingKnown) {
      // A rising edge only, and only from a KNOWN previous not-charging state --
      // tested against r.charging directly, in the same block that will go on to
      // set charging_, so nothing here depends on charging_ having been mutated
      // first. A first-ever sample that reads CHARGING cannot fire this, because
      // sawNotCharging_ starts false: that is what stops a boot with the cable
      // already in from adding a refresh to a boot that is already painting Home.
      //
      // It does NOT cover a boot that lands on the NOT-charging side of a
      // dithering signal instead: the very next sample reading charging is then
      // indistinguishable from a real plug-in and grants exactly as one would.
      // That is left open deliberately -- closing it breaks the feature's
      // primary case, "not-charging to charging asks for exactly one repaint" --
      // and kMaxGrantsPerSession is what bounds the cost. See "a boot onto the
      // dither's low side costs at most one grant" below.
      if (r.charging && sawNotCharging_ && !latched_ && grants_ < kMaxGrantsPerSession) {
        latched_ = true;
        ++grants_;
        repaintWanted_ = true;
      }
      charging_ = r.charging;
      haveCharging_ = true;
      if (charging_) {
        // Only sawNotCharging_ resets here -- notChargingSinceMs_ needs no reset
        // of its own, because the next not-charging sample always re-stamps it
        // through the !sawNotCharging_ arm below before it can ever be read.
        sawNotCharging_ = false;
      } else {
        // The dwell is measured from the FIRST not-charging sample in an unbroken
        // run, so any charging sample above resets it.
        if (!sawNotCharging_) {
          sawNotCharging_ = true;
          notChargingSinceMs_ = nowMs;
        } else if (latched_ &&
                   // Unsigned difference, so this is correct across the ~49-day
                   // millis() wrap, as every quiet-window gate in the shell is.
                   static_cast<uint32_t>(nowMs - notChargingSinceMs_) >= kUnlatchMs) {
          // THE CLEARING REPAINT. Reported from an X3: the bolt appears within
          // ~2 s of plugging in (correct) and then stays on glass indefinitely
          // after unplugging, because nothing repaints Home once charging_
          // goes false and the user is sitting on Home pressing nothing. The
          // design's original rule -- a falling edge never spends a refresh --
          // assumed a button press would correct it; sitting still, nothing
          // does. A stale "charging" claim is the same defect class this
          // project already refuses for an unread gauge (-1, not 0%) and a
          // book with no reading position (no demo substitute): a false claim
          // is worse than an absent one.
          //
          // This rides the SAME dwell that already tells a real unplug from
          // the gauge's zero-current dither, so it needs no new constant and
          // inherits the same by-construction argument as the latch itself: a
          // signal that cannot hold kUnlatchMs of unbroken not-charging can
          // never reach this branch, so a plain falling edge's flicker risk
          // does not come back.
          //
          // latched_ clears UNCONDITIONALLY -- it tracks reality, and must not
          // stay true just because the session ran out of repaint budget, or
          // the very next real plug-in would be refused as "already latched"
          // when it is not. Only the REPAINT is grant-gated, through the same
          // budget the rising edge spends -- so a full plug/unplug cycle can
          // now cost up to two grants instead of one. Accepted: three grants
          // was already a backstop against a hardware quirk this project
          // cannot bench-test, not a promise of exactly one refresh per cycle.
          latched_ = false;
          if (grants_ < kMaxGrantsPerSession) {
            ++grants_;
            repaintWanted_ = true;
          }
        }
      }
    }

    // --- The ladder ---------------------------------------------------------------
    //
    // AFTER the blocks above, so it reads the values this reading has already
    // installed rather than a second copy of them. One update(), one level: two
    // objects fed the same reading would be a caller list, and the shell would be
    // free to feed one and forget the other.
    //
    // A READING THAT DID NOT ANSWER CHANGES NOTHING. It cannot lower the level (a
    // bus glitch is not a flat pack) and it cannot advance the dwell (a dwell
    // satisfied by silence is a shutdown nothing confirmed). "Flat" and "did not
    // answer" stay different claims, exactly as percent()'s kUnknownPercent keeps
    // them.
    if (!r.percentKnown) return;

    if (percent_ > kLowPercent) {
      level_ = BatteryLevel::Normal;
      sawCriticalSinceMs_ = 0;
      inCriticalRun_ = false;
      return;
    }

    level_ = BatteryLevel::Low;

    // CHARGING SUPPRESSES CRITICAL AND NOT LOW. A device on the cable must not shut
    // down; but the battery IS low, and the banner saying so is true. On an X4
    // charging() is never known -- there is no charge-status pin -- so it never
    // suppresses there, and shutdown-then-refuse-to-wake is exactly right for a
    // flat X4 on a cable: the glass says CHARGE TO WAKE, and it does.
    if (percent_ > kCriticalPercent || charging()) {
      sawCriticalSinceMs_ = 0;
      inCriticalRun_ = false;
      return;
    }

    if (!inCriticalRun_) {
      inCriticalRun_ = true;
      sawCriticalSinceMs_ = nowMs;
      return;
    }
    // Unsigned difference, so this is correct across the ~49-day millis() wrap, as
    // kUnlatchMs's dwell and every quiet-window gate in the shell are.
    if (static_cast<uint32_t>(nowMs - sawCriticalSinceMs_) >= kCriticalDwellMs)
      level_ = BatteryLevel::Critical;
  }

  int percent() const { return havePercent_ ? percent_ : kUnknownPercent; }
  // false means "not charging" OR "never told", and unlike percent() there is no
  // third answer to give the caller: the only thing ever drawn is the mark's
  // presence or absence, so a device whose board has no charge-status line at
  // all (chargingKnown never true) must render identically to one that read a
  // known "not charging". There is nothing for a sentinel to buy here.
  bool charging() const { return haveCharging_ && charging_; }

  // True once per granted edge -- a plug-in, or an unplug the dwell has just
  // confirmed. TAKING IT CLEARS IT: the shell's paint-time read calls this and
  // discards the answer, because a paint is already happening and a surviving
  // request would fire a second refresh at the next poll, immediately after
  // the paint that already showed (or cleared) the bolt.
  bool takeRepaintRequest() {
    const bool wanted = repaintWanted_;
    repaintWanted_ = false;
    return wanted;
  }

 private:
  int percent_ = 0;
  bool havePercent_ = false;
  bool charging_ = false;
  bool haveCharging_ = false;
  bool sawNotCharging_ = false;
  bool latched_ = false;
  bool repaintWanted_ = false;
  int grants_ = 0;
  uint32_t notChargingSinceMs_ = 0;
  BatteryLevel level_ = BatteryLevel::Normal;
  bool inCriticalRun_ = false;
  uint32_t sawCriticalSinceMs_ = 0;
};

}  // namespace reader
