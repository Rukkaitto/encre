// The directory-listing cache, and the policy SdFileSystem drives it with.
//
// The cache exists because a 203-book /books lists in 590 ms on the device and
// the Library pays that on every push (reader/dir_cache.h has the measurement
// and why the walk itself cannot be made cheaper). It lives in core/ for the
// reason everything else load-bearing does: shell/ has no test harness, and five
// bugs have hidden there.
//
// TWO KINDS OF TEST HERE, and the second is the one that makes this safe.
// Everything up to "the caching policy" pins the store itself. After it,
// test_filesystem.cpp's contract runner is driven through a FileSystem that
// wraps the fake and uses the cache EXACTLY as SdFileSystem::list does -- so all
// 27 clauses run over the cached path and prove the cache is transparent. Both
// halves are needed: the unit tests would pass over a cache that answered the
// wrong directory, and the contract run would pass over a cache that never
// filled.
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/dir_cache.h"
#include "reader/filesystem.h"
#include "reader/fs_contract.h"

using reader::DirEntry;
using reader::DirListingCache;

namespace {

std::vector<DirEntry> entries(size_t n, const char* prefix = "book") {
  std::vector<DirEntry> v;
  for (size_t i = 0; i < n; ++i) {
    DirEntry e;
    e.name = std::string(prefix) + std::to_string(i) + ".epub";
    e.isDir = (i % 7) == 3;
    e.size = static_cast<uint32_t>(i * 1000 + 1);
    v.push_back(std::move(e));
  }
  return v;
}

// Everything the cache is asked to preserve about one entry.
bool same(const DirEntry& a, const DirEntry& b) {
  return a.name == b.name && a.isDir == b.isDir && a.size == b.size;
}

}  // namespace

TEST_CASE("a stored listing comes back entry for entry, in order") {
  DirListingCache c(0, 1u << 20);
  const std::vector<DirEntry> in = entries(40);
  REQUIRE(c.store("/books", in));

  std::vector<DirEntry> out;
  REQUIRE(c.appendTo("/books", out));
  REQUIRE(out.size() == in.size());
  for (size_t i = 0; i < in.size(); ++i) CHECK(same(out[i], in[i]));
}

TEST_CASE("appendTo APPENDS, as list() does, and leaves out alone on a miss") {
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", entries(3)));

  std::vector<DirEntry> out;
  out.push_back(DirEntry{"already-here", false, 9});
  REQUIRE(c.appendTo("/books", out));
  CHECK(out.size() == 4);
  CHECK(out[0].name == "already-here");
  CHECK(out[1].name == "book0.epub");

  // A miss must not touch what the caller already gathered -- the contract's
  // "list on a missing path leaves out alone" clause, one layer down.
  const size_t before = out.size();
  CHECK_FALSE(c.appendTo("/other", out));
  CHECK(out.size() == before);
}

TEST_CASE("only the path it was stored under answers, and no normalising is done") {
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", entries(3)));

  CHECK(c.holds("/books"));
  // A MISS IS ALWAYS SAFE. The cache does not normalise -- the normaliser
  // already exists in three copies and a fourth would be worse than a re-read --
  // so a second spelling of the same directory misses and the caller goes to the
  // card. That is the behaviour, and it is deliberate.
  CHECK_FALSE(c.holds("/books/"));
  CHECK_FALSE(c.holds("//books"));
  CHECK_FALSE(c.holds("/Books"));
  CHECK_FALSE(c.holds("/books/Classics"));
  CHECK_FALSE(c.holds(""));
}

TEST_CASE("two directories are held at once, which is what one rescan reads") {
  // LibraryScreen::rescan() lists /books and then /.reader/state. Holding only
  // one of them would mean the Library hit the cache exactly once, ever.
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", entries(5, "a")));
  REQUIRE(c.store("/.reader/state", entries(2, "b")));

  CHECK(c.holds("/books"));
  CHECK(c.holds("/.reader/state"));
  CHECK(c.slotsHeld() == 2);
  CHECK(c.entriesFor("/books") == 5);
  CHECK(c.entriesFor("/.reader/state") == 2);
}

TEST_CASE("storing a path again replaces that path's rows and nothing else") {
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", entries(5, "a")));
  REQUIRE(c.store("/.reader/state", entries(2, "b")));
  REQUIRE(c.store("/books", entries(9, "c")));

  CHECK(c.entriesFor("/books") == 9);
  CHECK(c.entriesFor("/.reader/state") == 2);
  std::vector<DirEntry> out;
  REQUIRE(c.appendTo("/books", out));
  CHECK(out.front().name == "c0.epub");
}

TEST_CASE("a THIRD directory evicts the SMALLEST, never the dear one") {
  // The rule that makes two slots enough on a card with folders: /books is
  // 590 ms and a folder listing is a few, so the folders fight over the second
  // slot and /books stays put. LRU would have thrown /books out here.
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", entries(200, "a")));
  REQUIRE(c.store("/.reader/state", entries(40, "b")));
  REQUIRE(c.store("/books/Classics", entries(6, "c")));

  CHECK(c.holds("/books"));            // untouched
  CHECK(c.holds("/books/Classics"));   // took the smaller slot
  CHECK_FALSE(c.holds("/.reader/state"));
}

TEST_CASE("no directory can be locked out of the cache by a bigger neighbour") {
  // The wedge that "refuse anything smaller than what is held" would have: once
  // a 500-entry folder took the slot, /books could never be cached again. A new
  // listing always gets a slot, so the worst case is one extra card walk.
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/big", entries(500, "a")));
  REQUIRE(c.store("/bigger", entries(600, "b")));
  REQUIRE(c.store("/books", entries(203, "c")));
  CHECK(c.holds("/books"));
}

TEST_CASE("clear forgets everything and is safe on an empty cache") {
  DirListingCache c(0, 1u << 20);
  c.clear();  // nothing held; must not fault
  CHECK_FALSE(c.holds("/books"));
  CHECK(c.residentBytes() == 0);

  REQUIRE(c.store("/books", entries(10)));
  CHECK(c.residentBytes() > 0);
  c.clear();
  CHECK_FALSE(c.holds("/books"));
  CHECK(c.slotsHeld() == 0);
  CHECK(c.residentBytes() == 0);
}

TEST_CASE("an EMPTY directory is a listing, not an absence") {
  // list() is careful to distinguish "true with nothing in it" from "false", and
  // so is this: with no minimum, an empty listing is storable and answers.
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/empty", {}));
  CHECK(c.holds("/empty"));

  std::vector<DirEntry> out;
  CHECK(c.appendTo("/empty", out));  // true...
  CHECK(out.empty());                // ...with nothing appended
}

TEST_CASE("a listing below the minimum is refused, so a cheap one cannot evict a dear one") {
  // The case this exists for: rescan() lists /books (203 entries) and then
  // /.reader/state (three on a real card). With one slot and no threshold the
  // cheap listing would be the one left in it, and the Library would pay the
  // card on every push exactly as it does today.
  DirListingCache c(32, 1u << 20);
  REQUIRE(c.store("/books", entries(203)));
  CHECK(c.holds("/books"));

  CHECK_FALSE(c.store("/.reader/state", entries(3)));
  CHECK_FALSE(c.holds("/.reader/state"));
  // ...and /books, which the refusal has learned nothing about, is untouched.
  CHECK(c.holds("/books"));
}

TEST_CASE("a refused store leaves THAT path uncached, never answering with the old rows") {
  // The one way this class could be silently wrong: the caller has just re-read
  // the directory off the card, so whatever was held for it is by definition
  // stale. Every refusal path has to drop it.
  DirListingCache c(0, 64);
  REQUIRE(c.store("/books", entries(1, "old")));
  CHECK(c.holds("/books"));

  // Same path, now too big for the ceiling: refused, and the old rows go.
  CHECK_FALSE(c.store("/books", entries(200, "new")));
  CHECK_FALSE(c.holds("/books"));
}

TEST_CASE("a store below the minimum drops that path's stale rows too") {
  DirListingCache c(32, 1u << 20);
  REQUIRE(c.store("/d", entries(40, "old")));
  CHECK(c.holds("/d"));
  // The directory shrank below the threshold. It must not keep answering with
  // the forty rows it used to have.
  CHECK_FALSE(c.store("/d", entries(5, "new")));
  CHECK_FALSE(c.holds("/d"));
}

TEST_CASE("the minimum is a count of entries, and the boundary is inclusive") {
  DirListingCache c(32, 1u << 20);
  CHECK_FALSE(c.store("/d", entries(31)));
  CHECK(c.store("/d", entries(32)));
}

TEST_CASE("the byte ceiling is across BOTH slots and is never exceeded") {
  DirListingCache c(0, 1024);
  CHECK(c.store("/small", entries(4)));
  CHECK(c.residentBytes() <= 1024);

  // Too big even with everything else evicted: refused, and it says so rather
  // than half-filling a slot.
  CHECK_FALSE(c.store("/big", entries(500)));
  CHECK_FALSE(c.holds("/big"));
  CHECK(c.residentBytes() <= 1024);

  // Two that fit only one at a time: the second gets in, the first makes way.
  DirListingCache d(0, 300);
  CHECK(d.store("/a", entries(8, "aaaaaaaaaa")));
  CHECK(d.store("/b", entries(8, "bbbbbbbbbb")));
  CHECK(d.residentBytes() <= 300);
  CHECK(d.holds("/b"));
}

TEST_CASE("residentBytes is the names plus the rows, exactly") {
  DirListingCache c(0, 1u << 20);
  const std::vector<DirEntry> in = entries(50);
  size_t nameBytes = 0;
  for (const DirEntry& e : in) nameBytes += e.name.size();
  REQUIRE(c.store("/books", in));
  // 12 bytes a row. Asserted as a range rather than a literal so that a
  // padding difference on some other target is not a failure, but a row that
  // grew to hold a whole std::string would be.
  CHECK(c.residentBytes() >= nameBytes + in.size() * 8);
  CHECK(c.residentBytes() <= nameBytes + in.size() * 16);
}

TEST_CASE("a whole rescan of a real card fits the device budget") {
  // Both directories one rescan() reads, at the sizes a 203-book card with
  // every book started would produce. This is what kDeviceMaxBytes is justified
  // against, so it is asserted rather than quoted.
  std::vector<DirEntry> books, state;
  for (int i = 0; i < 203; ++i) {
    DirEntry b;
    b.name = "Middlemarch - George Eliot " + std::to_string(i) + ".epub";
    b.size = 1234567;
    books.push_back(std::move(b));
    DirEntry s;
    s.name = "0a1b2c3d.json";  // the sidecar name: 8 hex plus the extension
    s.size = 190;
    state.push_back(std::move(s));
  }
  DirListingCache c(DirListingCache::kDeviceMinEntries, DirListingCache::kDeviceMaxBytes);
  REQUIRE(c.store("/books", books));
  REQUIRE(c.store("/.reader/state", state));
  CHECK(c.holds("/books"));
  CHECK(c.holds("/.reader/state"));
  CHECK(c.residentBytes() < DirListingCache::kDeviceMaxBytes);
}

TEST_CASE("a real card's shape fits the device budget with room") {
  // 203 books with names the length real ones have. This is the number the
  // header's kDeviceMaxBytes is justified against, so it is asserted rather
  // than quoted.
  std::vector<DirEntry> in;
  for (int i = 0; i < 203; ++i) {
    DirEntry e;
    e.name = "Middlemarch - George Eliot " + std::to_string(i) + ".epub";
    e.size = 1234567;
    in.push_back(std::move(e));
  }
  DirListingCache c(DirListingCache::kDeviceMinEntries, DirListingCache::kDeviceMaxBytes);
  REQUIRE(c.store("/books", in));
  CHECK(c.residentBytes() < 14u * 1024u);
  CHECK(c.entriesFor("/books") == 203);
}

TEST_CASE("names with every byte value survive, including one that is empty") {
  std::vector<DirEntry> in;
  DirEntry blank;  // name "", which no card produces and the contract does not forbid
  in.push_back(std::move(blank));
  DirEntry high;
  high.name = std::string("Le Fl\xC3\xA9""au.epub");
  high.size = 42;
  in.push_back(std::move(high));
  DirEntry nul;
  nul.name = std::string("a\0b", 3);
  in.push_back(std::move(nul));

  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", in));
  std::vector<DirEntry> out;
  REQUIRE(c.appendTo("/books", out));
  REQUIRE(out.size() == 3);
  for (size_t i = 0; i < 3; ++i) CHECK(same(out[i], in[i]));
}

TEST_CASE("hits and misses are counted, so a device log can say it is working") {
  DirListingCache c(0, 1u << 20);
  REQUIRE(c.store("/books", entries(4)));
  std::vector<DirEntry> out;
  CHECK(c.appendTo("/books", out));
  CHECK(c.appendTo("/books", out));  // a hit does NOT consume; Home counts, then the Library lists
  CHECK_FALSE(c.appendTo("/elsewhere", out));
  CHECK(c.hits() == 2);
  CHECK(c.misses() == 1);
}

// ---------------------------------------------------------------------------
// The caching policy, driven through the whole FileSystem contract.
//
// CachedListFs is SdFileSystem::list's cache handling and nothing else: the same
// calls in the same order at the same points. If a clause fails here it would
// fail on the card, which is the only way to check shell/src/sd_fs.cpp's policy
// on a desktop.
// ---------------------------------------------------------------------------

namespace {

class CachedListFs : public reader::FileSystem {
 public:
  // minEntries 0 -- the contract's directories hold a handful of files, and a
  // device threshold here would mean the cache never filled and 27 clauses
  // proved nothing about it. That is exactly why the threshold is a constructor
  // argument and not a constant.
  CachedListFs() : cache_(0, 1u << 20) {}

  bool mounted() const override { return inner_.mounted(); }
  bool exists(std::string_view p) override { return inner_.exists(p); }
  bool readAll(std::string_view p, std::string& out) override { return inner_.readAll(p, out); }

  bool list(std::string_view path, std::vector<DirEntry>& out) override {
    if (!mounted()) return false;
    const std::string p = FakeFileSystem::normalise(path);
    if (cache_.appendTo(p, out)) return true;
    std::vector<DirEntry> found;
    if (!inner_.list(p, found)) return false;
    // STORE, THEN SERVE FROM THE STORE. See SdFileSystem::list for why the fresh
    // listing goes back out through the cache instead of being appended
    // directly: it makes the cached answer and the fresh one literally the same
    // code, and here it is what puts every one of the 27 clauses through
    // appendTo -- most of them list a path once, so an adapter that appended
    // `found` on the miss would run the contract past a cache that never served.
    if (cache_.store(p, found)) return cache_.appendTo(p, out);
    out.insert(out.end(), std::make_move_iterator(found.begin()),
               std::make_move_iterator(found.end()));
    return true;
  }

  // A HANDLE MEANS A BOOK IS OPENING. openRead is the EPUB path -- readAll
  // serves the small JSON -- so this is where the cache gives its heap back,
  // before the reader needs a 32 KB inflate window out of a 45,840-byte floor.
  std::unique_ptr<reader::FileHandle> openRead(std::string_view p) override {
    cache_.clear();
    return inner_.openRead(p);
  }

  // The three mutators. Cleared BEFORE the call, so an early return inside the
  // operation cannot skip it, and regardless of the result -- a caller that
  // asked to change the card is reason enough to distrust a held listing.
  bool writeAll(std::string_view p, std::string_view d) override {
    cache_.clear();
    return inner_.writeAll(p, d);
  }
  bool mkdirs(std::string_view p) override {
    cache_.clear();
    return inner_.mkdirs(p);
  }
  bool remove(std::string_view p) override {
    cache_.clear();
    return inner_.remove(p);
  }

  FakeFileSystem& inner() { return inner_; }
  DirListingCache& cache() { return cache_; }

 private:
  FakeFileSystem inner_;
  DirListingCache cache_;
};

class DirCacheReport : public reader::FsContractReport {
 public:
  void check(bool passed, const char* expr) override { CHECK_MESSAGE(passed, std::string(expr)); }
  bool require(bool passed, const char* expr) override {
    REQUIRE_MESSAGE(passed, std::string(expr));
    return passed;
  }
};

}  // namespace

TEST_CASE("a FileSystem serving list() from the cache still obeys the whole contract") {
  INFO("implementation: FakeFileSystem behind DirListingCache");
  size_t count = 0;
  const reader::FsContractClause* clauses = reader::fsContractClauses(count);
  REQUIRE(count == 27);  // if a clause is added, this run must grow with it
  for (size_t i = 0; i < count; ++i) {
    SUBCASE(clauses[i].name) {
      CachedListFs fs;
      DirCacheReport report;
      reader::fsRunClause(clauses[i], fs, report);
    }
  }
}

TEST_CASE("with mounted() false, a cached listing does not answer for the card") {
  // The hazard the ordering in list() exists for: mounted() is checked BEFORE
  // the cache, so a card pulled after a successful listing cannot be answered
  // out of RAM. That is the same defect this project shipped once in the card
  // probe, which was served from SdFat's sector cache and reported success with
  // the card in the user's hand.
  CachedListFs fs;
  REQUIRE(fs.inner().writeAll("/books/a.epub", "a"));
  REQUIRE(fs.inner().writeAll("/books/b.epub", "b"));
  std::vector<DirEntry> warm;
  REQUIRE(fs.list("/books", warm));
  REQUIRE(fs.cache().holds("/books"));

  fs.inner().setMounted(false);
  std::vector<DirEntry> out;
  CHECK_FALSE(fs.list("/books", out));
  CHECK(out.empty());
}

TEST_CASE("the second listing of a directory does not reach the filesystem") {
  // The whole point, stated as a test: a cache that is never consulted passes
  // every clause above and buys nothing.
  struct CountingFs : FakeFileSystem {
    int lists = 0;
    bool list(std::string_view p, std::vector<DirEntry>& out) override {
      ++lists;
      return FakeFileSystem::list(p, out);
    }
  };

  CountingFs inner;
  REQUIRE(inner.writeAll("/books/a.epub", "a"));
  REQUIRE(inner.writeAll("/books/b.epub", "b"));

  DirListingCache cache(0, 1u << 20);
  auto listThrough = [&](const char* path, std::vector<DirEntry>& out) {
    if (cache.appendTo(path, out)) return true;
    std::vector<DirEntry> found;
    if (!inner.list(path, found)) return false;
    cache.store(path, found);
    out.insert(out.end(), found.begin(), found.end());
    return true;
  };

  std::vector<DirEntry> a, b;
  REQUIRE(listThrough("/books", a));
  CHECK(inner.lists == 1);
  REQUIRE(listThrough("/books", b));
  CHECK(inner.lists == 1);  // the card was not asked twice
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) CHECK(same(a[i], b[i]));

  // ...and a mutation puts it back on the card.
  cache.clear();
  std::vector<DirEntry> c;
  REQUIRE(listThrough("/books", c));
  CHECK(inner.lists == 2);
}
