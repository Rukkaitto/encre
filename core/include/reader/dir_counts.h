#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

// ONE NUMBER A CALLER DERIVED FROM A DIRECTORY, HELD UNTIL THE CARD CHANGES.
//
// THE DEFECT THIS EXISTS FOR, which is one shape wearing two issue numbers.
// A folder's book count is one directory listing, at the ~2.90 ms an ENTRY
// reader/dir_cache.h measures and explains, and two callers want the same number
// for the same folder:
//
//   * Home's `LIBRARY` row, through BookList::countLibrary, which lists /books
//     and then lists every subfolder of it -- at boot, before the first paint.
//   * The Library's own rescan(), through BookList::countBooks, once per folder
//     ROW, for the board's `FOLDER - 6 BOOKS` line -- on every push, because the
//     Library is destroyed by the pop that leaves it.
//
// On a flat card that is one extra listing and nobody notices. On a card with
// fifty folders it is fifty listings at boot and fifty more every time the user
// opens the Library, for numbers that have not changed. The listing cache does
// not help: a six-book folder is below its minimum entry count, and it has two
// slots against fifty directories.
//
// WHY A COUNT RATHER THAN A LISTING. The rows of a subfolder are never wanted --
// only how many of them are books. A count is 12 bytes plus its path where the
// listing is hundreds, so this holds fifty folders in ~1.5 KB where the listing
// cache's whole 20 KB ceiling would hold a handful. Cheaper, and it is the thing
// actually being asked for.
//
// IT DOES NOT KNOW WHAT THE NUMBER MEANS, deliberately. BookList decides what a
// book is; this only promises that an association dies when the directory could
// have changed. That is what lets it sit on the FileSystem -- the one object
// every mutation goes through -- without the storage layer learning about EPUBs.
//
// WHAT MAKES IT SAFE IS WHERE IT SITS, exactly as with the listing cache: an
// implementation hands one out from behind FileSystem::dirCounts(), and every way
// to change what is on the card is a method on that same object, so writeAll,
// mkdirs and remove each drop it before they touch anything. There is no path to
// a mutation that misses the invalidation. That is stronger than keying on a
// count of removals -- the key Home's own cached integer uses -- because that
// argument rests on "a book cannot ARRIVE while the firmware runs", and V2's
// Wi-Fi transfer is precisely the change that breaks it. A transfer writes
// through writeAll and drops this by construction, with no line to remember.
//
// A MISS IS ALWAYS SAFE, and that is the property to preserve when touching this.
// A number answers only for the exact path string it was stored under -- no
// normalising, because the normaliser already exists in three copies and a fourth
// is worse than a miss. Two spellings of one directory therefore miss and re-read
// the card, which is correct and merely slower. The only way to be WRONG is to
// answer with a stale number, and the invalidation above is what that is for.
class DirCountCache {
 public:
  // HOW MANY FOLDERS, and the number is a ceiling rather than a target. The issue
  // that prompted this says "fifty folders"; 64 covers it with room, and a card
  // wilder than that gets the first 64 and pays the card for the rest. At a
  // typical `/books/Science Fiction` the paths are ~22 bytes, so a full memo is
  // ~1.5 KB -- against the ~133 KB free with no book open, and 3% of the
  // 45,840-byte floor this project measured with one.
  static constexpr size_t kMaxEntries = 64;

  // And a ceiling on the paths themselves, because a path's length is the user's
  // to choose. Neither limit is an error: see remember().
  static constexpr size_t kMaxPathBytes = 2048;

  // COPYABLE, unlike DirListingCache, and the difference is ownership rather
  // than taste: that one holds two new[] blocks per slot and duplicating the
  // owner is how a buffer gets freed twice, where this holds a std::string and a
  // std::vector that copy themselves correctly. It matters because a FileSystem
  // that holds one must stay copyable -- three tests copy the in-memory fake, and
  // a memo is not a reason to change how a test double is passed around.

  // True with `out` set when `path`'s number is held. Counts as a hit or a miss
  // either way, so a device log can tell a memo that is working from one that has
  // quietly stopped answering.
  bool lookup(std::string_view path, int& out) {
    const Entry* e = find(path);
    if (e == nullptr) {
      ++misses_;
      return false;
    }
    ++hits_;
    out = e->value;
    return true;
  }

  // Associate `value` with `path` until the next clear().
  //
  // REFUSED IS NOT AN ERROR -- past kMaxEntries, or a path that would take the
  // blob past kMaxPathBytes. A refusal costs exactly what the card costs today,
  // and refusing rather than EVICTING is deliberate: the folders already held go
  // on hitting, so a card past the ceiling has a stable subset that is cheap
  // instead of a whole memo that thrashes.
  //
  // An existing entry for `path` is overwritten rather than duplicated, so a
  // caller that remembers twice cannot grow the memo or leave two answers in it.
  void remember(std::string_view path, int value) {
    if (Entry* e = find(path); e != nullptr) {
      e->value = value;
      return;
    }
    if (entries_.size() >= kMaxEntries) return;
    if (paths_.size() + path.size() > kMaxPathBytes) return;
    const uint32_t begin = static_cast<uint32_t>(paths_.size());
    paths_.append(path);
    entries_.push_back(Entry{begin, static_cast<uint16_t>(path.size()), value});
  }

  // Forget everything. Cheap and safe on an empty memo; the capacity is kept, so
  // the mutators that call it on every write do not churn the heap.
  //
  // hits() and misses() SURVIVE, deliberately: they are a session total, and a
  // counter that reset on every invalidation could not answer "is this doing
  // anything" -- which is the only question they exist for. held() is the one that
  // falls back to zero, and that is how a log line distinguishes an invalidation
  // from a memo that stopped being consulted.
  void clear() {
    entries_.clear();
    paths_.clear();
  }

  size_t held() const { return entries_.size(); }
  size_t bytes() const { return paths_.size() + entries_.size() * sizeof(Entry); }

  // So a log can say whether this is doing anything. A hit produces no `[fs]
  // list` line at all, so the ABSENCE of that line is the only visible symptom of
  // success -- and it is indistinguishable from the absence of the call. This
  // project has already shipped a card probe that reported success while doing
  // nothing.
  size_t hits() const { return hits_; }
  size_t misses() const { return misses_; }

 private:
  // 12 bytes. The path lives in one blob rather than in a std::string per entry:
  // sixty-four small allocations is the shape that fragments this part's heap
  // into something the reader's 32 KB inflate window cannot fit into, which is
  // the same reasoning DirListingCache's Row records.
  struct Entry {
    uint32_t pathBegin;
    uint16_t pathLength;
    int value;
  };

  Entry* find(std::string_view path) {
    for (Entry& e : entries_)
      if (std::string_view(paths_).substr(e.pathBegin, e.pathLength) == path) return &e;
    return nullptr;
  }

  std::string paths_;
  std::vector<Entry> entries_;
  size_t hits_ = 0;
  size_t misses_ = 0;
};

}  // namespace reader
