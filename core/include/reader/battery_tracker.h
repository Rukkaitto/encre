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

// WHAT THE BAND SHOULD SAY, AND WHETHER PLUGGING IN IS WORTH A REPAINT.
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
  static constexpr int kMaxGrantsPerSession = 3;

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
          latched_ = false;
        }
      }
    }
  }

  int percent() const { return havePercent_ ? percent_ : kUnknownPercent; }
  // false means "not charging" OR "never told", and unlike percent() there is no
  // third answer to give the caller: the only thing ever drawn is the mark's
  // presence or absence, so a device whose board has no charge-status line at
  // all (chargingKnown never true) must render identically to one that read a
  // known "not charging". There is nothing for a sentinel to buy here.
  bool charging() const { return haveCharging_ && charging_; }

  // True once per granted rising edge. TAKING IT CLEARS IT: the shell's paint-time
  // read calls this and discards the answer, because a paint is already happening
  // and a surviving request would fire a second refresh at the next poll,
  // immediately after the paint that already showed the bolt.
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
};

}  // namespace reader
