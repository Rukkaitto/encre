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

  // Re-checks the card and updates mounted(). Real traffic on the shared bus, so
  // it is deliberately NOT called from mounted(); see the comment on mounted()
  // in the .cpp for what it can and cannot detect.
  //
  // WHAT IT READS IS THE WHOLE POINT, and getting it wrong is a defect this
  // project has already shipped once. See ProbeTarget.
  bool probe();

  // What probe() reads to reach the card, and the difference between a probe
  // that works and one that only looks like it does.
  //
  // THE DEFECT, confirmed on hardware: probe() used to open "/" -- the ROOT
  // DIRECTORY -- and the root directory's sector is the one sector guaranteed to
  // be sitting in SdFat's cache after boot. SdFat on this build has exactly ONE
  // 512-byte sector cache (FsCache in common/FsCache.h holds a single
  // `m_buffer[512]`, and USE_SEPARATE_FAT_CACHE is compiled OFF here because it
  // is gated on __arm__ and the ESP32-C3 is RISC-V), so a repeated root read is
  // answered out of RAM and keeps succeeding with the card physically out of the
  // slot. Pulling the card produced no log line and never reached the SD-missing
  // screen.
  //
  //   * File -- open a real file and read a byte off it. That walks the ROOT
  //     DIRECTORY sector, then the SUBDIRECTORY's sector, then the file's DATA
  //     sector: three distinct sectors, one 512-byte cache, so at most one of
  //     them can be served from RAM. Better still, the sector the cache holds
  //     when a probe ENDS (the file's data) is not the one the next probe needs
  //     FIRST (the root directory), so in the steady state every one of the
  //     three is a real card read.
  //   * RootDir -- the old behaviour, kept only as a fallback for when no target
  //     file can be established. It is NOT a card-detect and must be announced
  //     as degraded wherever it is in force, because a probe that silently falls
  //     back to it is this same bug again.
  enum class ProbeTarget { RootDir, File };

  // Point probe() at `path`, and say whether it took.
  //
  // Adopted ONLY if the file can be opened and a byte read off it right now. A
  // target that is absent, is a directory, or is empty would make every probe
  // fail, which is strictly worse than the cached root read -- it would report a
  // card that is sitting right there as gone, and route the UI to the SD-missing
  // screen for a missing FILE. On refusal the target stays RootDir.
  bool useFileProbeTarget(const char* path);
  ProbeTarget probeTarget() const { return probeTarget_; }
  // The adopted path, or "" while the target is RootDir.
  const char* probeTargetPath() const { return probeTargetPath_.c_str(); }

  // THE BACKSTOP, because the fast probe above is still inference: it argues
  // from SdFat's cache geometry that the sectors cannot all be in RAM. This
  // argues from nothing.
  //
  // SDCardManager::sdUsedBytes() calls FsVolume::freeClusterCount(), which scans
  // the ENTIRE FAT one sector at a time (FatPartition::freeClusterCount, and
  // MAINTAIN_FREE_CLUSTER_COUNT is 0 in this build so there is no shortcut
  // return). Thousands of sectors against one 512-byte cache: if the card is
  // gone it cannot answer, and there is no cache geometry to reason about.
  //
  // It is expensive -- that is the same reason the SDK caches it for 20 s -- so
  // it is the slow layer under the fast probe, not a replacement for it.
  //
  // armDeepProbe() records the baseline and must be called once after a mount is
  // confirmed. It returns false if the scan cannot produce a non-zero byte
  // count, in which case deepProbe() has no opinion and the caller must say so:
  // sdUsedBytes() reports a FAILED scan as 0, so a volume that legitimately read
  // 0 would be indistinguishable from a dead card forever.
  bool armDeepProbe();
  bool deepProbeArmed() const { return deepBaseline_ != 0; }
  uint64_t deepProbeBaselineBytes() const { return deepBaseline_; }
  // Runs the FAT scan and updates mounted(). Meaningless unless
  // deepProbeArmed(); returns true (no opinion) in that case.
  bool deepProbe();

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
  // The two halves of probe(). Neither takes the bus guard -- probe() holds it.
  bool readProbeTargetFile();
  bool readRootDirectory();

  bool live_ = false;
  size_t skippedNames_ = 0;
  ProbeTarget probeTarget_ = ProbeTarget::RootDir;
  std::string probeTargetPath_;
  // Bytes the FAT scan reported at arm time. 0 means "not armed".
  uint64_t deepBaseline_ = 0;
};
