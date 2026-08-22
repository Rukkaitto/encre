#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/booklist.h"

using namespace reader;

namespace {

// A FileSystem that returns exactly what the fake holds, in a deliberately
// hostile order.
//
// The fake's own list() walks a std::map and a std::set, so it hands back
// directories first and each group in byte order -- which is very nearly the
// answer BookList is supposed to produce. Testing the sort against it would
// therefore pass on a scan that did no sorting at all. FAT gives no ordering
// guarantee whatsoever, and "a listing whose order depends on the filesystem is
// a listing that looks different on two cards with the same books" is the whole
// reason the sort exists, so the test drives it through this.
//
// The shuffle is a fixed-seed xorshift: a test that is only sometimes hostile is
// a test that only sometimes fails.
class ShuffledFs : public FileSystem {
 public:
  explicit ShuffledFs(FakeFileSystem& inner, uint32_t seed = 0x1234567u)
      : inner_(inner), seed_(seed) {}

  bool mounted() const override { return inner_.mounted(); }
  bool exists(std::string_view p) override { return inner_.exists(p); }
  bool readAll(std::string_view p, std::string& o) override { return inner_.readAll(p, o); }
  bool writeAll(std::string_view p, std::string_view d) override { return inner_.writeAll(p, d); }
  bool mkdirs(std::string_view p) override { return inner_.mkdirs(p); }
  bool remove(std::string_view p) override { return inner_.remove(p); }
  std::unique_ptr<FileHandle> openRead(std::string_view p) override {
    return inner_.openRead(p);
  }

  bool list(std::string_view path, std::vector<DirEntry>& out) override {
    std::vector<DirEntry> mine;
    if (!inner_.list(path, mine)) return false;
    // Fisher-Yates, walked backwards, so the fake's near-sorted order is
    // thoroughly destroyed rather than nudged.
    for (size_t i = mine.size(); i > 1; --i) {
      const size_t j = next() % i;
      std::swap(mine[i - 1], mine[j]);
    }
    // The contract says list APPENDS.
    out.insert(out.end(), mine.begin(), mine.end());
    return true;
  }

 private:
  uint32_t next() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
  }
  FakeFileSystem& inner_;
  uint32_t seed_;
};

// The names in `out`, joined -- what a failure should print.
std::string names(const std::vector<BookEntry>& out) {
  std::string s;
  for (const auto& e : out) {
    if (!s.empty()) s += ", ";
    s += e.name;
  }
  return s;
}

bool has(const std::vector<BookEntry>& out, std::string_view name) {
  for (const auto& e : out)
    if (e.name == name) return true;
  return false;
}

const BookEntry* find(const std::vector<BookEntry>& out, std::string_view name) {
  for (const auto& e : out)
    if (e.name == name) return &e;
  return nullptr;
}

}  // namespace

TEST_CASE("epub and txt are books, in any case the card spells them") {
  // FAT is case-preserving but not case-sensitive, and a card written on a Mac
  // will have a .EPUB on it eventually.
  FakeFileSystem fs;
  fs.writeAll("/books/one.epub", "x");
  fs.writeAll("/books/two.EPUB", "x");
  fs.writeAll("/books/three.Epub", "x");
  fs.writeAll("/books/four.txt", "x");
  fs.writeAll("/books/five.TXT", "x");
  fs.writeAll("/books/six.Txt", "x");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  CAPTURE(names(out));
  CHECK(out.size() == 6);
}

TEST_CASE("other extensions and extensionless files are skipped") {
  FakeFileSystem fs;
  fs.writeAll("/books/keep.epub", "x");
  fs.writeAll("/books/cover.jpg", "x");
  fs.writeAll("/books/notes.pdf", "x");
  fs.writeAll("/books/README", "x");
  fs.writeAll("/books/archive.epub.bak", "x");  // the FINAL extension is what counts
  fs.writeAll("/books/trailing.", "x");
  fs.writeAll("/books/epub", "x");  // the word, not an extension
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  CAPTURE(names(out));
  CHECK(out.size() == 1);
  CHECK(out[0].name == "keep.epub");
}

TEST_CASE("directories are rows whatever they are called") {
  // A folder is a place to descend into, so its name says nothing about whether
  // it belongs on the list -- including when it ends in something that looks
  // like a rejected extension.
  FakeFileSystem fs;
  fs.mkdirs("/books/Sci-Fi");
  fs.mkdirs("/books/covers.jpg");
  fs.mkdirs("/books/no-extension-here");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  CAPTURE(names(out));
  CHECK(out.size() == 3);
  for (const auto& e : out) CHECK(e.isDir);
  // A folder keeps its whole name as its title: it has no extension to strip,
  // and "covers" would be a different folder than the one on the card.
  CHECK(find(out, "covers.jpg")->title() == "covers.jpg");
}

TEST_CASE("folders sort before books, then each group alphabetically and case-blind") {
  FakeFileSystem fs;
  fs.mkdirs("/books/zebra");
  fs.mkdirs("/books/Anthology");
  fs.mkdirs("/books/middle");
  fs.writeAll("/books/banana.epub", "x");
  fs.writeAll("/books/Apple.epub", "x");
  fs.writeAll("/books/cherry.txt", "x");
  fs.writeAll("/books/Date.EPUB", "x");
  ShuffledFs hostile(fs);
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(hostile, "/books", out));
  CAPTURE(names(out));
  REQUIRE(out.size() == 7);
  CHECK(out[0].name == "Anthology");
  CHECK(out[1].name == "middle");
  CHECK(out[2].name == "zebra");
  CHECK(out[3].name == "Apple.epub");
  CHECK(out[4].name == "banana.epub");
  CHECK(out[5].name == "cherry.txt");
  CHECK(out[6].name == "Date.EPUB");
  // Case-blind, not byte order. Byte order puts every capital ahead of every
  // lowercase, so it would have produced "Date.EPUB" before "banana.epub" --
  // and a list where capitals bunch at the top reads as unsorted to anyone
  // looking at the screen.
  CHECK(std::string("Date.EPUB") < std::string("banana.epub"));  // what bytes say
  CHECK(out[4].name == "banana.epub");                           // what we do
  CHECK(out[6].name == "Date.EPUB");
}

TEST_CASE("the order does not depend on which order the card lists things in") {
  // Same books, six different filesystem orders, one answer.
  FakeFileSystem fs;
  fs.mkdirs("/books/Folder B");
  fs.mkdirs("/books/folder a");
  fs.writeAll("/books/Beta.epub", "x");
  fs.writeAll("/books/alpha.txt", "x");
  fs.writeAll("/books/Gamma.EPUB", "x");
  std::string first;
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    ShuffledFs hostile(fs, seed * 2654435761u + 1u);
    std::vector<BookEntry> out;
    REQUIRE(BookList::scan(hostile, "/books", out));
    if (seed == 1) first = names(out);
    CAPTURE(seed);
    CHECK(names(out) == first);
  }
  CHECK(first == "folder a, Folder B, alpha.txt, Beta.epub, Gamma.EPUB");
}

TEST_CASE("names that differ only in case get one fixed order, not an arbitrary one") {
  // std::sort is not stable, so a comparator that calls these two equal would
  // let the order come out of the sort's internals. Two cards with the same
  // books must still look the same.
  FakeFileSystem fs;
  fs.writeAll("/books/Novel.epub", "x");
  fs.writeAll("/books/novel.txt", "x");
  fs.writeAll("/books/NOVEL.epub", "x");
  std::string first;
  for (uint32_t seed = 1; seed <= 8; ++seed) {
    ShuffledFs hostile(fs, seed * 40503u + 7u);
    std::vector<BookEntry> out;
    REQUIRE(BookList::scan(hostile, "/books", out));
    REQUIRE(out.size() == 3);
    if (seed == 1) first = names(out);
    CAPTURE(seed);
    CHECK(names(out) == first);
  }
}

TEST_CASE("the title is the filename minus its final extension, and nothing cleverer") {
  // A placeholder until Phase 3 reads the real title out of the EPUB. No
  // underscore-to-space, no title-casing: a clever transform would make a wrong
  // title look deliberate.
  FakeFileSystem fs;
  fs.writeAll("/books/Middlemarch.epub", "x");
  fs.writeAll("/books/Vol.2.epub", "x");
  fs.writeAll("/books/the_waves.txt", "x");
  fs.writeAll("/books/a.b.c.epub", "x");
  fs.writeAll("/books/UPPER CASE.EPUB", "x");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  CAPTURE(names(out));
  REQUIRE(out.size() == 5);
  CHECK(find(out, "Middlemarch.epub")->title() == "Middlemarch");
  CHECK(find(out, "Vol.2.epub")->title() == "Vol.2");
  CHECK(find(out, "the_waves.txt")->title() == "the_waves");
  CHECK(find(out, "a.b.c.epub")->title() == "a.b.c");
  CHECK(find(out, "UPPER CASE.EPUB")->title() == "UPPER CASE");
}

TEST_CASE("hidden entries are skipped, files and directories alike") {
  // A dotfile is not a book. This matters more than it sounds: a card that has
  // ever been mounted on a Mac carries AppleDouble sidecars named `._Book.epub`
  // -- which end in .epub and would otherwise appear as a phantom duplicate of
  // every real book -- plus `.Spotlight-V100`, `.fseventsd` and `.Trashes`
  // DIRECTORIES, which would appear as folders to descend into.
  FakeFileSystem fs;
  fs.writeAll("/books/Real.epub", "x");
  fs.writeAll("/books/._Real.epub", "x");
  fs.writeAll("/books/.hidden.epub", "x");
  fs.writeAll("/books/.DS_Store", "x");
  fs.mkdirs("/books/.Spotlight-V100");
  fs.mkdirs("/books/.fseventsd");
  fs.mkdirs("/books/Visible");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  CAPTURE(names(out));
  CHECK(out.size() == 2);
  CHECK(has(out, "Visible"));
  CHECK(has(out, "Real.epub"));
}

TEST_CASE("an empty directory is an empty list and TRUE") {
  // No books is a valid state. The caller has to be able to tell it from a read
  // failure, because one draws an empty library and the other is a broken card.
  FakeFileSystem fs;
  fs.mkdirs("/books");
  std::vector<BookEntry> out;
  CHECK(BookList::scan(fs, "/books", out));
  CHECK(out.empty());
}

TEST_CASE("a directory holding nothing that qualifies is also empty and TRUE") {
  FakeFileSystem fs;
  fs.writeAll("/books/.DS_Store", "x");
  fs.writeAll("/books/cover.jpg", "x");
  std::vector<BookEntry> out;
  CHECK(BookList::scan(fs, "/books", out));
  CHECK(out.empty());
}

TEST_CASE("a path that is not a directory is false") {
  FakeFileSystem fs;
  fs.writeAll("/books/one.epub", "x");
  std::vector<BookEntry> out;
  CHECK_FALSE(BookList::scan(fs, "/books/one.epub", out));
  CHECK(out.empty());
  CHECK_FALSE(BookList::scan(fs, "/nowhere", out));
  CHECK(out.empty());
}

TEST_CASE("an unmounted filesystem is false, not empty-and-fine") {
  // The distinction the SD-missing screen exists for: no card is not an empty
  // library.
  FakeFileSystem fs;
  fs.writeAll("/books/one.epub", "x");
  fs.setMounted(false);
  std::vector<BookEntry> out;
  CHECK_FALSE(BookList::scan(fs, "/books", out));
  CHECK(out.empty());
}

TEST_CASE("scan clears its output first, so a rescan does not accumulate") {
  FakeFileSystem fs;
  fs.writeAll("/books/one.epub", "x");
  fs.writeAll("/books/two.epub", "x");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  REQUIRE(out.size() == 2);
  REQUIRE(BookList::scan(fs, "/books", out));
  CHECK(out.size() == 2);
  // ...and a failed scan clears it too: the list is what the card says now, and
  // leaving the previous card's books on screen is worse than showing none.
  fs.setMounted(false);
  CHECK_FALSE(BookList::scan(fs, "/books", out));
  CHECK(out.empty());
}

TEST_CASE("a book carries its size and a folder does not pretend to have one") {
  FakeFileSystem fs;
  fs.writeAll("/books/sized.epub", std::string(4321, 'x'));
  fs.mkdirs("/books/folder");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books", out));
  REQUIRE(out.size() == 2);
  CHECK(find(out, "sized.epub")->size == 4321u);
  CHECK_FALSE(find(out, "sized.epub")->isDir);
  CHECK(find(out, "folder")->size == 0u);
  CHECK(find(out, "folder")->isDir);
}

TEST_CASE("the name is the leaf, not the path it was found under") {
  // The row's name is joined onto the directory to open the book; a name that
  // already carried the directory would address /books/books/one.epub.
  FakeFileSystem fs;
  fs.writeAll("/books/nested/deep/one.epub", "x");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/books/nested/deep", out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].name == "one.epub");
}

TEST_CASE("the root directory is scannable like any other") {
  // A card whose books are loose at the top level, and the path shape most
  // likely to trip a naive join.
  FakeFileSystem fs;
  fs.writeAll("/loose.epub", "x");
  fs.mkdirs("/books");
  std::vector<BookEntry> out;
  REQUIRE(BookList::scan(fs, "/", out));
  CAPTURE(names(out));
  CHECK(out.size() == 2);
  CHECK(out[0].name == "books");
  CHECK(out[1].name == "loose.epub");
}

// --- countLibrary, which is Home's LIBRARY row ----------------------------

TEST_CASE("countLibrary counts the books here plus the books one level down") {
  // The board's own arithmetic: design/Library.dc.html says `12 BOOKS` over six
  // books and a folder holding six more.
  FakeFileSystem fs;
  for (int i = 0; i < 6; ++i)
    fs.writeAll("/books/loose" + std::to_string(i) + ".epub", "x");
  for (int i = 0; i < 6; ++i)
    fs.writeAll("/books/Classics/inside" + std::to_string(i) + ".epub", "x");
  CHECK(BookList::countLibrary(fs, "/books") == 12);
}

TEST_CASE("countLibrary stops at one level, as the board's count does") {
  // A book two levels down is inside a folder the user has to open a folder to
  // reach, and the design's number does not promise to have walked there. A
  // recursive walk of a card is also unbounded work on a screen that has to
  // paint.
  FakeFileSystem fs;
  fs.writeAll("/books/top.epub", "x");
  fs.writeAll("/books/one/mid.epub", "x");
  fs.writeAll("/books/one/two/deep.epub", "x");
  CHECK(BookList::countLibrary(fs, "/books") == 2);
}

TEST_CASE("countLibrary counts no books, and that is not the same as -1") {
  FakeFileSystem fs;
  fs.mkdirs("/books");
  // An empty library is a valid state: 0 is a count, and the row can show it.
  CHECK(BookList::countLibrary(fs, "/books") == 0);
  // A directory that is not there cannot be counted, and 0 would be a claim
  // about a card nobody read. Home draws nothing at all for this.
  CHECK(BookList::countLibrary(fs, "/nothing-here") == -1);
  FakeFileSystem gone;
  gone.setMounted(false);
  CHECK(BookList::countLibrary(gone, "/books") == -1);
}

TEST_CASE("countLibrary ignores what the listing ignores") {
  FakeFileSystem fs;
  fs.writeAll("/books/real.epub", "x");
  fs.writeAll("/books/notes.pdf", "x");           // wrong extension
  fs.writeAll("/books/._real.epub", "x");         // an AppleDouble sidecar
  fs.writeAll("/books/.Trashes/junk.epub", "x");  // a hidden DIRECTORY
  CHECK(BookList::countLibrary(fs, "/books") == 1);
}

TEST_CASE("countLibrary survives a folder it cannot look inside") {
  // A folder whose listing fails must contribute nothing rather than subtracting
  // its -1 from the total, which would make the count smaller than the books
  // plainly visible beside it.
  class OneBadDir : public FakeFileSystem {
   public:
    bool list(std::string_view path, std::vector<DirEntry>& out) override {
      if (path == "/books/bad") return false;
      return FakeFileSystem::list(path, out);
    }
  };
  OneBadDir fs;
  fs.writeAll("/books/good.epub", "x");
  fs.mkdirs("/books/bad");
  CHECK(BookList::countLibrary(fs, "/books") == 1);
}

// --- The title is a view, and the override is the Phase 3 seam ---------------
//
// BookEntry used to carry a second std::string per book, heap-allocated on every
// name past the 15-char small-string buffer -- which is essentially all of them.
// The derived title is always a PREFIX of the name, so it costs nothing to hand
// back a view instead. These pin both halves: that the common case allocates
// nothing, and that the override still works, so Phase 3 can fill titles from
// EPUB metadata without rediscovering how.

TEST_CASE("the derived title is a view INTO the name, not a copy") {
  reader::BookEntry e{"Middlemarch.epub", std::string(), false, 1234};
  const std::string_view t = e.title();
  CHECK(t == "Middlemarch");
  // The whole point: same storage. If this ever fails, every book on the card is
  // paying for a second allocation again.
  CHECK(t.data() == e.name.data());
}

TEST_CASE("a folder's title is the whole name, still a view") {
  reader::BookEntry e{"Classics", std::string(), true, 0};
  CHECK(e.title() == "Classics");
  CHECK(e.title().data() == e.name.data());
}

TEST_CASE("titleOverride wins when set -- the Phase 3 path") {
  // An EPUB whose metadata title has nothing to do with its filename, which is
  // the normal case for a Calibre-managed card.
  reader::BookEntry e{"middlemarch_1871.epub", "Middlemarch", false, 99};
  CHECK(e.title() == "Middlemarch");
  CHECK(e.title().data() == e.titleOverride.data());
}

TEST_CASE("an override is sorted on, not the filename") {
  // The comparator reads title(), so an override has to change the order or the
  // list would sort by something the screen does not show.
  std::vector<reader::BookEntry> rows{
      {"zzz.epub", "Aardvark", false, 1},
      {"aaa.epub", std::string(), false, 1},  // derives to "aaa"
  };
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
    return a.title() < b.title();
  });
  CHECK(rows[0].title() == "Aardvark");
  CHECK(rows[1].title() == "aaa");
}

TEST_CASE("countLibrary counts what scan lists, without building or sorting it") {
  // countLibrary works off the raw listing now; scan() builds BookEntries and
  // sorts them. The two must still agree exactly, because one is a band's number
  // and the other is the rows underneath it -- and a band that disagreed with
  // its own rows is worse than no band.
  FakeFileSystem fs;
  fs.writeAll("/books/Middlemarch.epub", "x");
  fs.writeAll("/books/Walden.epub", "x");
  fs.writeAll("/books/.hidden.epub", "x");   // excluded by both
  fs.writeAll("/books/notes.txt", "x");      // .txt IS a book here
  fs.writeAll("/books/cover.jpg", "x");      // excluded by both
  fs.writeAll("/books/Classics/Emma.epub", "x");
  fs.writeAll("/books/Classics/Persuasion.epub", "x");

  std::vector<reader::BookEntry> rows;
  REQUIRE(reader::BookList::scan(fs, "/books", rows));
  int viaScan = 0;
  for (const auto& e : rows) {
    if (!e.isDir) { ++viaScan; continue; }
    const int inside = reader::BookList::countBooks(fs, "/books/" + e.name);
    if (inside > 0) viaScan += inside;
  }

  CHECK(reader::BookList::countLibrary(fs, "/books") == viaScan);
  CHECK(viaScan == 5);  // 3 beside the folder + 2 inside it
}
