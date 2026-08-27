#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "reader/dir_cache.h"
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
// APPEND BYTES TO A FILE ON THE CARD. Returns false on any failure, and says
// nothing about why -- the one caller is the diagnostic log, which must never be
// able to break the thing it is observing.
//
// A FREE FUNCTION AND NOT A FileSystem METHOD, deliberately. `reader::FileSystem`
// has no append and should not grow one for this: its contract is 27 clauses driven
// by two harnesses (test_filesystem.cpp on the desktop, sd_selftest.cpp against a
// real card), and widening it means widening both for something `core/` will never
// call. The log is a shell concern from end to end.
//
// `capBytes` restarts the file rather than growing it forever: past that size it is
// truncated and reopened, so a device left running cannot fill the card. Losing the
// oldest half of a log is a fair price for that, and the alternative -- refusing to
// write once full -- loses the NEWEST, which is the half you want.
//
// Takes the bus guard itself. The caller still has to choose a moment when the
// panel is idle: this makes the write safe, not free.
bool appendToCard(const char* path, const char* data, size_t len, uint32_t capBytes);

class SpiBusGuard {
 public:
  SpiBusGuard();
  ~SpiBusGuard();
  SpiBusGuard(const SpiBusGuard&) = delete;
  SpiBusGuard& operator=(const SpiBusGuard&) = delete;
};

// The read handle, defined in sd_fs.cpp. Declared here only so SdFileSystem can
// befriend it: it needs noteCardGone(), because a read that stops short of the
// file's own declared size is the same "the card stopped answering" evidence
// readAll acts on, and a handle that swallowed it would leave mounted() lying.
class SdFileHandle;

// THE BUS GUARD AND A LONG-LIVED HANDLE: per-operation, never per-handle.
//
// A handle is a new shape for this class. Every other method here is one bounded
// burst of card traffic, so wrapping the whole method in a SpiBusGuard is both
// correct and free. A handle is different: an EPUB reader holds one open for as
// long as the book is open -- minutes, and across many paints.
//
// So the guard is taken per OPERATION (open, each read, each seek, close) and
// never held for the handle's lifetime. Holding it across the lifetime would not
// deadlock today -- the mutex is recursive and both users are on the Arduino loop
// task, so renderTop() taking it again would simply succeed -- but that is the
// accident that makes it look safe. The moment a handle is held by anything OTHER
// than the paint task (a background library scan is the obvious Phase 3
// candidate, and it is exactly why this guard exists at all) renderTop() would
// block behind it for the whole time a book is open: not a deadlock, a panel that
// never repaints. Which is worse, because it looks like a display fault.
//
// What makes per-operation sufficient is that SdFat holds no bus state between
// calls. An open FsFile is RAM only -- a cluster number, an offset, some flags --
// so nothing is asserted on the bus while nobody is inside a call. size() and
// position() do not touch the card at all: both are answered from members this
// class caches, which is why neither takes the guard.
//
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
  //
  // COUNTED ON A CARD WALK, NOT ON EVERY CALL. A listing served out of the cache
  // below does not re-count, because the entries were dropped before the store
  // and are not in it. So this is "names this build could not address, per real
  // read of the card", which is the question it was written to answer.
  size_t skippedNames() const { return skippedNames_; }

  // How many times remove() has been ASKED to delete something, whether or not
  // that call is the one that changed the card.
  //
  // Deliberately bumped before the refusals rather than on success: a caller
  // that asked to delete a book is reason enough for anything holding a derived
  // view of the tree -- Home's book count is the other one -- to distrust it. A
  // counter rather than a flag so a reader can tell "nothing has changed" from
  // "something changed and was already dealt with".
  uint32_t removals() const { return removals_; }

  // THE OTHER DERIVED VIEW IS HOME'S `LIBRARY` COUNT, and it is keyed on this.
  // It is one directory listing plus one per folder for a single integer, on the
  // critical path of a Back out of a book -- see libraryCountForHome() in
  // shell/src/main.cpp. It is NOT held by this class the way the listing cache
  // is, because it is not a listing: it is an answer the layer above derives from
  // one, so this counter is what tells that layer to derive it again.

  // The held directory listings, for a log line. See list() for what fills them
  // and reader/dir_cache.h for why they exist at all.
  //
  // WORTH PRINTING SOMEWHERE. `hits()`, `misses()`, `slotsHeld()` and
  // `residentBytes()` are the only way to tell a cache that is working from one
  // that has quietly stopped: a hit produces no `[fs] list` line at all, so the
  // absence of that line is the ONLY visible symptom of success, and it is
  // indistinguishable from the absence of the call. This project has already
  // shipped a card probe that reported success while doing nothing.
  const reader::DirListingCache& listings() const { return listings_; }

  // Longest leaf name list() will report, including the terminator.
  //
  // FAT's LFN ceiling is 255 UTF-16 units, which is up to 765 bytes of UTF-8, so
  // this does not cover every nameable file -- it covers every ASCII or Latin
  // name and about 85 characters of CJK, for 256 bytes of stack instead of 768.
  // What makes the shortfall safe is that SdFat's getName() returns 0 rather
  // than truncating when the name does not fit (see list()).
  static constexpr size_t kNameBufBytes = 256;

  // readAll's ceiling. The interface is explicit that readAll is for the small
  // JSON files V1 stores and that EPUBs need openRead instead, and this is where
  // that stops being advice: the firmware is built -fno-exceptions, so a
  // std::string::resize that cannot allocate calls abort() and takes the device
  // down with no diagnostic. A 64 KB cap turns "someone put a 300 MB file where a
  // settings file goes" into a clean false.
  //
  // openRead has NO equivalent cap and needs none: the handle allocates a fixed
  // few dozen bytes whatever the file's size, and the buffer is the caller's.
  // That is the whole difference between the two.
  static constexpr uint32_t kMaxReadBytes = 64u * 1024u;

  bool mounted() const override;
  bool exists(std::string_view path) override;
  bool list(std::string_view path, std::vector<reader::DirEntry>& out) override;
  bool readAll(std::string_view path, std::string& out) override;
  std::unique_ptr<reader::FileHandle> openRead(std::string_view path) override;
  bool writeAll(std::string_view path, std::string_view data) override;
  bool mkdirs(std::string_view path) override;
  bool remove(std::string_view path) override;

 private:
  friend class SdFileHandle;

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
  uint32_t removals_ = 0;

  // THE ONE DIRECTORY LISTING THIS CLASS HOLDS. See reader/dir_cache.h for the
  // measurement, for why the walk itself cannot be made cheaper, and for the
  // memory argument behind the two constants.
  //
  // The reason it belongs to this object rather than to the Library screen that
  // suffers from the cost: every way to change what is on the card is a method
  // on this class, so writeAll, mkdirs and remove can each drop it and there is
  // no mutation path that misses the invalidation. A cache the Library owned
  // would have to be told, and a caller list maintained in prose is a function
  // not yet written.
  reader::DirListingCache listings_{reader::DirListingCache::kDeviceMinEntries,
                                   reader::DirListingCache::kDeviceMaxBytes};
  ProbeTarget probeTarget_ = ProbeTarget::RootDir;
  std::string probeTargetPath_;
  // Bytes the FAT scan reported at arm time. 0 means "not armed".
  uint64_t deepBaseline_ = 0;
};
