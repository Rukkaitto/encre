#pragma once
// THE FAKES' SHARED STATE, so a scenario can drive them and read them back.
//
// This directory is SCAFFOLDING (#178, #179). It exists so the desktop can
// compile and drive shell/src/main.cpp at all, and it is meant to SHRINK: every
// port extracted in #182 deletes the fake it replaces, and #183 removes what is
// left. `wc -l test/shell/fake_arduino/*.h` is the number to watch.
//
// THE RULE THAT KEEPS IT HONEST: nothing gets faked in order to compile an
// already-adapted shell file. Seven of the eight non-main.cpp files in shell/ are
// adapters at a reader:: seam, and those get desktop TWINS at the seam rather than
// a fake underneath them. Faking SdFat properly to compile the real sd_fs.cpp is
// what would turn this into a second firmware nothing checks.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace harness {

// THE TRANSCRIPT IS THE TEST SURFACE. Every fake appends a line; the scenario
// compares the whole record. Determinism is BY CONSTRUCTION rather than by
// filtering afterwards -- a post-filter would be a second place the truth lives.
inline std::vector<std::string>& transcript() {
  static std::vector<std::string> t;
  return t;
}

inline void record(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  transcript().emplace_back(buf);
}

// A VIRTUAL CLOCK, zero at entry and advanced only by the panel model's per-
// waveform charge and by explicit script steps. That is what lets the `[i]` line's
// wait=/pre=/disp=/post= fields be asserted at all: with a real clock they are
// noise, and a golden over noise gets re-blessed, which the Goldens section of
// CLAUDE.md forbids for exactly this reason.
struct Clock {
  uint64_t ms = 0;
  void advance(uint64_t by) { ms += by; }
};
inline Clock& clock_() {
  static Clock c;
  return c;
}

// A SCRIPTED HEAP. Deterministic so `heap=` and `min=` on every [stage] line are
// checkable, and drivable so the heap-floor branches -- restartIfHeapSpent, the
// ring sizing -- can be reached at all. The real figures are measured on glass and
// nothing here is evidence about them.
struct Heap {
  uint32_t free_ = 180000;
  uint32_t min_ = 45840;   // the floor CLAUDE.md records after the walkToChapter fix
  uint32_t block_ = 61428; // the largest free block on a cold boot, per the TLS note
};
inline Heap& heap() {
  static Heap h;
  return h;
}

// WHAT THE POWER BUTTON READS. The wake gate's only input, so a scenario sets this
// and the gate is drivable without a finger.
inline bool& powerButtonDown() {
  static bool down = false;
  return down;
}

// Reset reason and wake cause, scripted. `[boot] reset reason=...` is the line
// that distinguishes a real resume from a host having reset the chip, and it is
// the first thing to read before believing anything about a wake.
inline int& resetReason() {
  static int r = 1;  // ESP_RST_POWERON
  return r;
}
inline int& wakeCause() {
  static int c = 0;  // ESP_SLEEP_WAKEUP_UNDEFINED
  return c;
}

// Whether a USB host has the port open. setup() leaves its CDC wait as soon as
// this is true, and gives up after a 400ms grace when isPlugged() says nothing is
// there -- so a scenario decides which of those two paths boot takes.
inline bool& usbHostPresent() {
  static bool present = false;
  return present;
}
inline bool& usbPlugged() {
  static bool plugged = false;
  return plugged;
}

// THE CARD: a host directory, and whether it is present at all. Here rather than in
// SDCardManager.h because sd_fs_host.cpp needs them and must NOT include that
// header -- SdFat is precisely what the card twin exists to keep out of the build.
inline std::string& cardRoot() {
  static std::string root;
  return root;
}
inline bool& cardPresent() {
  static bool present = true;
  return present;
}

// QUEUE A BUTTON TRANSITION for input_task_host.cpp's scripted queue. A press is
// TWO of these: a Short fires on the DOWN edge, and the release is what classifies
// a press made entirely inside a repaint -- which a gray refresh makes possible,
// since tick() only runs from the main loop.
void queueButton(uint8_t button, bool down, uint32_t atMs);

// RESET EVERY FAKE. One scenario per process is the rule (every piece of state in
// main.cpp is a file-static with a boot-time initialiser and there is no reset
// function; writing one would be a change to untested code before the net exists),
// so this is for the fakes' own tests rather than for scenarios.
void resetAll();

}  // namespace harness
