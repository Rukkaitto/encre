#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"

// THE SHARED SPI BUS.
//
// On the X3 the SD card and the display controller are on one SPI bus (MISO 7,
// display CS 21, card CS 12) and SDCardManager has NO locking of any kind --
// there is no mutex or semaphore anywhere in it. Its only shared-bus handling is
// in begin(), which drives the display's CS high before probing because a
// powered, never-deselected panel breaks card detection. Everything after that
// is the caller's problem: a card transfer that overlaps a panel refresh is a
// fault that looks random.
//
// So the invariant is "SD traffic and panel traffic never overlap", and this is
// where it is made structural instead of a comment someone has to remember.
// Every public method of SdFileSystem takes this guard for its whole duration,
// including the multi-step ones (writeAll is mkdirs + open + write + sync), so a
// caller CANNOT issue card traffic without holding the bus. The other half is one
// line in the shell: renderTop() must take a SpiBusGuard around the entire paint
// sequence -- the driver keeps CS asserted across its BUSY waits, so the guard
// has to cover the waits too, not just the SPI writes.
//
// The mutex is RECURSIVE on purpose: writeAll calls mkdirs, which takes it again.
//
// Worth knowing before deciding this is over-engineering: today both users run on
// the Arduino loop task -- paints happen in renderTop() and card access will
// happen from the same loop -- so they are already serialised by having one
// thread, and the only other task (input_task.cpp) touches ADC and GPIO only.
// The guard is therefore insurance rather than a fix, and it is cheap insurance:
// the day anything moves off that task (a background library scan is the obvious
// candidate for Phase 3, and a cover-image decode is the next) the fault it
// prevents is intermittent, bus-level and miserable to find.
class SpiBusGuard {
 public:
  SpiBusGuard();
  ~SpiBusGuard();
  SpiBusGuard(const SpiBusGuard&) = delete;
  SpiBusGuard& operator=(const SpiBusGuard&) = delete;
};

// reader::FileSystem over the SD card, via SDCardManager (which is a singleton,
// so this class holds no volume of its own and two instances would address the
// same card).
//
// Held to the same contract as the desktop implementations: the clauses in
// reader/fs_contract.h run against this one too, over Serial, via
// sd_selftest.h. `shell/` has no test harness, so that routine is the only thing
// standing between this file and a contract violation nobody notices.
class SdFileSystem : public reader::FileSystem {
 public:
  // Mounts the card. Safe to call again after a failure -- which is what the
  // SD-missing screen's retry does.
  //
  // NOTE, and it is a real limitation: SDCardManager::begin() short-circuits on
  // its own `initialized` flag and the SPI path exposes no end()/unmount(), so a
  // card that mounted once and was then PULLED cannot be re-mounted without a
  // reboot -- begin() returns true immediately without touching the card. Retry
  // works for the case the screen exists for (no card at boot); a hot-swap does
  // not, and pretending otherwise in the UI would be a lie.
  bool mount();

  // Re-checks the card and updates mounted(). Costs one directory read on the
  // shared bus, so it is deliberately NOT called from mounted(): see the comment
  // on mounted() below for what it can and cannot detect.
  bool probe();

  // Entries list() dropped because their name did not fit kNameBufBytes. Non-zero
  // means the user has a file this build cannot address; see list().
  size_t skippedNames() const { return skippedNames_; }

  // Longest leaf name list() will report, including the terminator.
  //
  // FAT's LFN ceiling is 255 UTF-16 units, which is up to 765 bytes of UTF-8, so
  // this does not cover every nameable file -- it covers every ASCII or Latin
  // name and about 85 characters of CJK, for 256 bytes of stack instead of 768.
  // What makes the shortfall safe is that SdFat's getName() returns 0 rather
  // than truncating when the name does not fit (see list()).
  static constexpr size_t kNameBufBytes = 256;

  // readAll's ceiling. The interface is explicit that readAll is for the small
  // JSON files V1 stores and that EPUBs will need a streaming handle instead, and
  // this is where that stops being advice: the firmware is built
  // -fno-exceptions, so a std::string::resize that cannot allocate calls abort()
  // and takes the device down with no diagnostic. A 64 KB cap turns "someone put
  // a 300 MB file where a settings file goes" into a clean false.
  static constexpr uint32_t kMaxReadBytes = 64u * 1024u;

  bool mounted() const override;
  bool exists(std::string_view path) override;
  bool list(std::string_view path, std::vector<reader::DirEntry>& out) override;
  bool readAll(std::string_view path, std::string& out) override;
  bool writeAll(std::string_view path, std::string_view data) override;
  bool mkdirs(std::string_view path) override;
  bool remove(std::string_view path) override;

 private:
  // True when `p` names an existing directory. Opens and closes a handle.
  bool isDirectory(const std::string& p);
  // True when `p` exists and is NOT a directory.
  bool isFile(const std::string& p);
  // Something failed in a way only a card that is no longer answering explains.
  // Clears mounted() so the next caller is told the truth rather than being let
  // through to fail again.
  void noteCardGone(const char* where);

  bool live_ = false;
  size_t skippedNames_ = 0;
};
