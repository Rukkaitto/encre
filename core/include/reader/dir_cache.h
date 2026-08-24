#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// DIRECTORY LISTINGS, HELD SO THE CARD IS NOT ASKED FOR THE SAME ONE TWICE.
//
// THE DEFECT THIS EXISTS FOR, measured on an X3 with a 203-book card:
//
//   [fs] list /books -> 203 entries in 590ms (2.90 ms/entry)
//   [i] #2 CONFIRM SHORT from=HOME to=LIBRARY ev=1 | wait=0 pre=0 disp=659
//           post=4 render=105 up=25 wave=414 | total=1208ms
//
// An ordinary chrome interaction on this device is 505-550 ms and 439 ms of that
// is the panel, which is irreducible. The Library push is more than twice any
// other navigation and ALL of the excess is one directory listing inside
// App::dispatch -- the push constructs LibraryScreen, whose constructor calls
// rescan(), which calls BookList::scan, which calls FileSystem::list. The
// Library is destroyed by the pop that leaves it, so Home > Library > Back >
// Library pays it twice, and Home's own book count pays it a third time.
//
// WHY THE LISTING ITSELF CANNOT BE MADE FASTER, which was the other direction
// and would have been the better outcome because it costs no RAM. The 2.90 ms an
// entry is inside SdFat and there is nothing left in our loop to remove:
//
//   * FatFile::getName8() -- the only API that yields a long filename -- opens a
//     SECOND FatFile over the directory's first cluster (openCluster, which
//     memsets it to position 0) and then calls cacheDir(m_dirIndex - order) once
//     per LFN entry, with the index DECREASING. cacheDir is seekSet + read, and
//     seekSet takes its "must follow chain from first cluster" branch whenever
//     the target is behind the current position -- which a decreasing index
//     always is. So every LFN entry of every name re-walks the directory's
//     cluster chain from its start, one FatPartition::fatGet per cluster.
//   * USE_SEPARATE_FAT_CACHE is gated on __arm__ and the C3 is RISC-V, so
//     fatCachePrepare IS dataCachePrepare: there is ONE 512-byte buffer, and the
//     FAT sector each fatGet needs and the directory sector the entry lives in
//     evict each other, turn for turn.
//   * The rest of the per-entry work is already free. isDirectory() and
//     fileSize() are RAM reads off the open handle, and close() reaches
//     SdSpiCard::syncDevice(), which returns true without touching the bus
//     unless a read or write stream is open. list() calls getName() exactly
//     once per entry and closes on every path out.
//
// So the only ways to attack the walk are to edit SdFat (a submodule this
// project does not edit) or to decode FAT and exFAT directory entries ourselves
// out of raw sectors, in shell/, which has no test harness. Not making the walk
// twice was the affordable answer.
//
// WHAT MAKES IT SAFE IS WHERE IT SITS, not an argument about what V1 can do. It
// is held by SdFileSystem, and every way to change what is on the card is a
// method on that same object: writeAll, mkdirs and remove each drop it before
// they touch anything. There is no path to a mutation that misses the
// invalidation -- which is stronger than keying on a count of removals, because
// that argument rests on "a book cannot ARRIVE while the firmware runs", and
// V2's Wi-Fi transfer is precisely the change that breaks it. A transfer writes
// through writeAll and drops this by construction.
//
// A MISS IS ALWAYS SAFE, and that is the property to preserve when touching
// this. A listing answers only for the exact path string it was stored under --
// there is deliberately no normalising here, because the normaliser already
// exists in three copies and a fourth is worse than a cache miss. Two spellings
// of one directory therefore miss and re-read the card, which is correct and
// merely slower. The only way to be WRONG is to answer with stale content, and
// that is what the invalidation above is for.
class DirListingCache {
 public:
  // TWO SLOTS, AND THE SECOND ONE IS NOT SPARE CAPACITY -- it is the difference
  // between this working on every card and working on mine.
  //
  // LibraryScreen::rescan() lists TWO directories, in this order: /books (203
  // entries, 590 ms) and then /.reader/state, which holds one sidecar per book
  // the user has ever OPENED. With one slot the second listing would take the
  // slot on every rescan, so the Library would hit the cache exactly once and
  // pay the card on every push after that. A minimum entry count hides that
  // only until a keen reader has started more books than the minimum, and an
  // optimisation that quietly stops working on a well-used device is the shape
  // of defect this project keeps paying for.
  //
  // Two is enough because two is how many directories one rescan reads. A card
  // with FOLDERS lists one more per folder, and those churn the second slot
  // between them -- /books is safe because eviction takes the SMALLEST held
  // listing, which is what "do not throw away the expensive one" means when it
  // has to be a rule rather than a hope.
  static constexpr size_t kSlots = 2;

  // minEntries -- below this a listing is not worth holding at all. At the
  // measured 2.90 ms an entry, 32 entries is ~93 ms, a fifth of one panel
  // waveform.
  //
  // maxBytes -- the ceiling on everything held, ACROSS slots, not per slot.
  //
  // Both are constructor arguments rather than constants because the desktop
  // test that drives the whole FileSystem contract through this policy has to be
  // able to cache everything: a device threshold there would mean the contract
  // run never filled a slot, and a check that reports on less than it claims is
  // worse than no check.
  DirListingCache(size_t minEntries, size_t maxBytes)
      : minEntries_(minEntries), maxBytes_(maxBytes) {}

  DirListingCache(const DirListingCache&) = delete;
  DirListingCache& operator=(const DirListingCache&) = delete;

  static constexpr size_t kDeviceMinEntries = 32;

  // The device's ceiling across both slots, and the number is measured against
  // the heap rather than chosen.
  //
  // A real 203-book /books packs to ~10.5 KB here: the names are ~40 bytes each
  // in one blob (8.1 KB) and a row is 12 bytes (2.4 KB). Two allocations per
  // slot, both exact -- which matters as much as the total, because on this part
  // the LARGEST FREE BLOCK decides and 203 separate std::strings would fragment
  // the heap into the shape a chapter's 32 KB inflate window cannot fit.
  // /.reader/state at its worst is one 13-byte name per book, ~5 KB for the same
  // 203. 20 KB holds both with room and refuses anything wilder.
  //
  // IT IS NEVER RESIDENT AT THE 45,840-BYTE FLOOR THIS PROJECT MEASURED WITH A
  // BOOK OPEN: openRead() drops everything, and openRead is the EPUB path -- the
  // only thing in this firmware that opens a file handle is a book, since
  // readAll serves the small JSON. So the peak this adds to is the peak with no
  // book open, which is ~133 KB free. A directory too big for the ceiling is
  // simply not held and costs exactly what it costs today.
  static constexpr size_t kDeviceMaxBytes = 20u * 1024u;

  // A name longer than this is not storable, because a row records its length in
  // 16 bits. FAT's LFN ceiling is 255 UTF-16 units and SdFileSystem's own name
  // buffer is 256 bytes, so nothing the device can list comes close; the check
  // is here because HostFileSystem and the fake have no such limit and a silent
  // truncation would hand back the wrong file.
  static constexpr size_t kMaxNameBytes = 65535;

  // Hold `entries` as `path`'s listing. True when it took.
  //
  // REFUSED IS NOT AN ERROR -- too few entries, too big for the ceiling even
  // alone, a name that will not fit, or an allocation that would not serve.
  // Whatever happens, any listing previously held FOR THIS PATH is released
  // first, so a refusal can never leave an older answer standing for a path the
  // caller has just re-read. That is the one failure mode that would be
  // silently wrong; the other slot is untouched, because it answers for a
  // different directory and nothing here has learned anything about it.
  bool store(std::string_view path, const std::vector<DirEntry>& entries);

  // APPENDS `path`'s entries to `out` and returns true, or returns false and
  // leaves `out` exactly as it was. Appending rather than replacing is
  // FileSystem::list's own contract, so a caller can substitute this for the
  // card without changing what its own caller sees.
  bool appendTo(std::string_view path, std::vector<DirEntry>& out);

  // Forget everything, every slot. Cheap and safe on an empty cache.
  void clear();

  // True when a listing for `path` is held -- the same string comparison
  // appendTo makes.
  bool holds(std::string_view path) const;

  // How many entries are held for `path`, or 0. Diagnostics; use holds() to tell
  // "not held" from "held, and empty".
  size_t entriesFor(std::string_view path) const;

  // How many slots are occupied, and what they cost right now. residentBytes is
  // exact, not an estimate: no allocation here over-allocates.
  size_t slotsHeld() const;
  size_t residentBytes() const;

  // So a device log can say whether the cache is doing anything. A cache that
  // silently stopped working reads exactly like one that never did.
  size_t hits() const { return hits_; }
  size_t misses() const { return misses_; }

 private:
  // 12 bytes: 4 + 4 + 2 + 1, padded to 12. The offset is stored rather than
  // derived from the previous row's end so that a bug in one row cannot walk
  // every row after it off the blob.
  struct Row {
    uint32_t nameBegin;
    uint32_t size;
    uint16_t nameLength;
    bool isDir;
  };

  struct Slot {
    // NOT "it has rows". An EMPTY directory is a listing like any other -- true
    // with nothing in it, which FileSystem::list is careful to distinguish from
    // false -- and with minEntries 0 it is storable. Deriving "held" from the
    // row count would quietly refuse to serve exactly that case.
    bool held = false;
    std::string path;
    // Allocated with new(std::nothrow) and never grown. The firmware is
    // -fno-exceptions, so an ordinary allocation that cannot be served is an
    // abort() with no diagnostic; a cache is the last thing that should be able
    // to take the device down, so it asks and accepts no for an answer.
    std::unique_ptr<char[]> names;
    std::unique_ptr<Row[]> rows;
    size_t nameBytes = 0;
    size_t rowCount = 0;

    void release();
    size_t bytes() const { return nameBytes + rowCount * sizeof(Row); }
  };

  const Slot* find(std::string_view path) const;
  Slot* find(std::string_view path);
  // An unoccupied slot, or null.
  Slot* freeSlot();
  // The HELD slot with the fewest entries, or null when none is held.
  //
  // Two helpers rather than one "pick a victim", because store() needs both
  // answers and needs them apart: it takes a free slot if there is one, and it
  // then evicts in a LOOP to make room for the bytes. A single function that
  // preferred a free slot made that loop a no-op -- it kept being handed the
  // slot store() had just released -- so the ceiling was quietly not enforced.
  // A test caught it; the shapes are separate now so it cannot come back.
  Slot* smallestHeld();

  size_t minEntries_;
  size_t maxBytes_;
  Slot slots_[kSlots];
  size_t hits_ = 0;
  size_t misses_ = 0;
};

}  // namespace reader
