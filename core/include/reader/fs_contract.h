#pragma once
// The FileSystem contract, written once and driven by more than one runner.
//
// Why this is a header in core/ rather than a block of doctest cases: `shell/`
// has no test harness, so SdFileSystem -- the ONE implementation that talks to
// real hardware, on the one bus that can go wrong -- is the one nothing would
// hold to the promises in filesystem.h. The clauses below report through
// FsContractReport instead of a test macro, so the same assertions run two ways:
//
//   * test/unit/test_filesystem.cpp adapts the report to doctest and drives it
//     against FakeFileSystem and HostFileSystem, on every `make test`.
//   * shell/src/sd_selftest.cpp adapts it to Serial and drives it against a real
//     card on the device, built in only with -DENCRE_FS_SELFTEST=1.
//
// core/ is the only place both of those can include from, which is the whole
// reason a self-test lives here. It is header-only and includes nothing beyond
// the standard library and filesystem.h, so a firmware build that does not
// include it pays nothing for it.
//
// EVERY CLAUSE NEEDS A MOUNTED, EMPTY FILESYSTEM, and clauses are independent:
// the runner hands each one fresh storage. doctest re-enters its SUBCASE with a
// new instance; the device runner wipes its scratch directory between clauses.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// Where a clause's assertions go. A runner implements this; the clauses know
// nothing about doctest or about Serial.
class FsContractReport {
 public:
  virtual ~FsContractReport() = default;

  // One assertion whose failure still leaves the rest of the clause meaningful.
  virtual void check(bool passed, const char* expr) = 0;

  // One the clause cannot continue without -- the equivalent of doctest's
  // REQUIRE. Returns `passed`; the clause returns immediately when it is false,
  // so a runner whose report cannot unwind (no exceptions on the device) still
  // stops at the same point the desktop one does.
  virtual bool require(bool passed, const char* expr) = 0;
};

// One independent case. `name` is what a runner labels it with -- it is also a
// doctest SUBCASE name, so keep it filesystem-safe-ish and human.
struct FsContractClause {
  const char* name;
  void (*run)(FileSystem& fs, FsContractReport& r);
};

// list()'s order is unspecified, so every assertion about a listing goes through
// here.
inline std::vector<DirEntry> fsSortedByName(std::vector<DirEntry> v) {
  std::sort(v.begin(), v.end(),
            [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return v;
}

inline const DirEntry* fsEntryNamed(const std::vector<DirEntry>& v, const std::string& name) {
  for (const auto& e : v)
    if (e.name == name) return &e;
  return nullptr;
}

// A body of `n` bytes that is not a repeating run, so a read landing one byte or
// one sector out is visible rather than looking correct. The stride is odd and
// coprime with 256, 512 and 4096, so no offset in a sector, a cluster or a chunk
// boundary shares a byte value with the same offset in the next one -- which is
// exactly the mistake this catches. Includes NULs and bytes above 0x7F, so it
// doubles as a binary-safety check on the handle path.
inline std::string fsPatternBody(size_t n) {
  std::string s;
  s.reserve(n);
  for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>((i * 31u + 7u) & 0xFFu));
  return s;
}

// Reads the whole of `h` in `chunk`-sized bites and returns what it got. A read
// that stops short of size() before the end means the read FAILED, not that the
// file ended, so the loop stops on a zero return and the caller compares lengths.
inline std::string fsDrain(FileHandle& h, size_t chunk) {
  std::string out;
  std::vector<char> buf(chunk);
  for (;;) {
    const size_t got = h.read(buf.data(), buf.size());
    if (got == 0) break;
    out.append(buf.data(), got);
  }
  return out;
}

// The two assertion forms, spelled so that the reported text is the source text.
// Both are #undef'd at the end of the header: nothing outside it should see them.
#define FSC_CHECK(cond) r.check(static_cast<bool>(cond), #cond)
#define FSC_REQUIRE(cond)                                   \
  do {                                                      \
    if (!r.require(static_cast<bool>(cond), #cond)) return;  \
  } while (false)

namespace fs_contract {

inline void roundTrip(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/hello.txt", "hello"));
  FSC_CHECK(fs.exists("/hello.txt"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/hello.txt", got));
  FSC_CHECK(got == "hello");
}

inline void binarySafe(FileSystem& fs, FsContractReport& r) {
  const std::string body("li\nne\0with\r\nnul", 15);
  FSC_REQUIRE(fs.writeAll("/binary.bin", body));
  std::string got;
  FSC_REQUIRE(fs.readAll("/binary.bin", got));
  FSC_CHECK(got.size() == body.size());
  FSC_CHECK(got == body);
}

inline void emptyFileIsAFile(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/empty.txt", ""));
  FSC_CHECK(fs.exists("/empty.txt"));
  std::string got = "sentinel";
  FSC_REQUIRE(fs.readAll("/empty.txt", got));
  FSC_CHECK(got.empty());
  std::vector<DirEntry> out;
  FSC_REQUIRE(fs.list("/", out));
  const DirEntry* e = fsEntryNamed(out, "empty.txt");
  FSC_REQUIRE(e != nullptr);
  FSC_CHECK(e->size == 0u);
  FSC_CHECK(!e->isDir);
}

inline void writeTruncates(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/t.txt", "a long original body"));
  FSC_REQUIRE(fs.writeAll("/t.txt", "short"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/t.txt", got));
  FSC_CHECK(got == "short");
}

inline void writeCreatesParents(FileSystem& fs, FsContractReport& r) {
  FSC_CHECK(!fs.exists("/.reader"));
  FSC_REQUIRE(fs.writeAll("/.reader/deep/settings.json", "{}"));
  FSC_CHECK(fs.exists("/.reader"));
  FSC_CHECK(fs.exists("/.reader/deep"));
  FSC_CHECK(fs.exists("/.reader/deep/settings.json"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/.reader/deep/settings.json", got));
  FSC_CHECK(got == "{}");
}

inline void listAppendsAndReports(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/books/sub"));
  FSC_REQUIRE(fs.writeAll("/books/a.txt", "12345"));

  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  FSC_REQUIRE(fs.list("/books", out));
  FSC_REQUIRE(out.size() == 3);
  FSC_CHECK(out[0].name == "pre-existing");  // appended, not cleared

  const auto s = fsSortedByName(std::vector<DirEntry>(out.begin() + 1, out.end()));
  FSC_CHECK(s[0].name == "a.txt");
  FSC_CHECK(!s[0].isDir);
  FSC_CHECK(s[0].size == 5u);
  FSC_CHECK(s[1].name == "sub");
  FSC_CHECK(s[1].isDir);
  FSC_CHECK(s[1].size == 0u);  // 0 for directories
}

inline void listNamesLeaves(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/dir/leaf.txt", "x"));
  std::vector<DirEntry> out;
  FSC_REQUIRE(fs.list("/dir", out));
  FSC_REQUIRE(out.size() == 1);
  FSC_CHECK(out[0].name == "leaf.txt");
}

inline void listRefusesFileAndMissing(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/file.txt", "x"));
  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  FSC_CHECK(!fs.list("/file.txt", out));
  FSC_CHECK(out.size() == 1);
  FSC_CHECK(!fs.list("/nope", out));
  FSC_CHECK(out.size() == 1);
  FSC_CHECK(!fs.list("/nope/deeper", out));
  FSC_CHECK(out.size() == 1);
}

inline void rootIsListable(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/top.txt", "x"));
  std::vector<DirEntry> out;
  FSC_REQUIRE(fs.list("/", out));
  FSC_CHECK(fsEntryNamed(out, "top.txt") != nullptr);
}

inline void readAllMissingLeavesOut(FileSystem& fs, FsContractReport& r) {
  std::string got = "sentinel";
  FSC_CHECK(!fs.readAll("/absent.txt", got));
  FSC_CHECK(got == "sentinel");
  // ...including when the parent directory does not exist either.
  FSC_CHECK(!fs.readAll("/no/such/dir/absent.txt", got));
  FSC_CHECK(got == "sentinel");
}

inline void readAllRefusesDirectory(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/adir"));
  std::string got = "sentinel";
  FSC_CHECK(!fs.readAll("/adir", got));
  FSC_CHECK(got == "sentinel");
}

inline void removeIsAboutTheEndState(FileSystem& fs, FsContractReport& r) {
  FSC_CHECK(fs.remove("/never-existed.txt"));  // already gone counts as removed
  FSC_REQUIRE(fs.writeAll("/doomed.txt", "x"));
  FSC_CHECK(fs.remove("/doomed.txt"));
  FSC_CHECK(!fs.exists("/doomed.txt"));
  FSC_CHECK(fs.remove("/doomed.txt"));  // and again, idempotently
}

inline void removeRefusesDirectory(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/keepme"));
  FSC_CHECK(!fs.remove("/keepme"));
  FSC_CHECK(fs.exists("/keepme"));
}

inline void mkdirsIsIdempotent(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/a/b/c"));
  FSC_CHECK(fs.exists("/a"));
  FSC_CHECK(fs.exists("/a/b"));
  FSC_CHECK(fs.exists("/a/b/c"));
  FSC_CHECK(fs.mkdirs("/a/b/c"));  // already there
  FSC_CHECK(fs.mkdirs("/"));       // the root always exists
}

inline void fileCannotBeAParent(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/blocker", "x"));
  FSC_CHECK(!fs.mkdirs("/blocker/under"));
  FSC_CHECK(!fs.writeAll("/blocker/under/f.txt", "x"));
  FSC_CHECK(!fs.exists("/blocker/under"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/blocker", got));
  FSC_CHECK(got == "x");  // and the file itself is untouched
}

inline void writeRefusesDirectory(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/adir"));
  FSC_CHECK(!fs.writeAll("/adir", "x"));
  FSC_CHECK(fs.exists("/adir"));
  std::vector<DirEntry> out;
  FSC_CHECK(fs.list("/adir", out));  // still a directory
}

inline void separatorsNormalise(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/n/f.txt", "x"));
  FSC_CHECK(fs.exists("/n//f.txt"));
  FSC_CHECK(fs.exists("/n/"));
  FSC_CHECK(fs.exists("/n/f.txt/"));  // stripped, not treated as a directory
  std::string got;
  FSC_REQUIRE(fs.readAll("//n//f.txt", got));
  FSC_CHECK(got == "x");
  std::vector<DirEntry> out;
  FSC_CHECK(fs.list("/n/", out));
}

// --- openRead / FileHandle ------------------------------------------------
//
// The EPUB path. readAll is not it, and never was: an archive is megabytes
// against ~71 KB of free heap, and a zip's central directory is at the END of
// the file, so the reader has to seek. These clauses are what stops the one
// implementation nothing else checks -- SdFileSystem, on the bus the panel
// shares -- from being subtly different from the two the desktop suite drives.

// A full read through the handle IS readAll's bytes. If these two ever disagree
// the handle is reading the wrong file, or the wrong part of it, and everything
// layered above will blame the parser.
inline void handleFullReadMatchesReadAll(FileSystem& fs, FsContractReport& r) {
  const std::string body = fsPatternBody(300);
  FSC_REQUIRE(fs.writeAll("/h/full.bin", body));

  std::string viaReadAll;
  FSC_REQUIRE(fs.readAll("/h/full.bin", viaReadAll));

  std::unique_ptr<FileHandle> h = fs.openRead("/h/full.bin");
  FSC_REQUIRE(h != nullptr);
  FSC_CHECK(h->size() == body.size());
  FSC_CHECK(h->position() == 0u);

  const std::string viaHandle = fsDrain(*h, 64);
  FSC_CHECK(viaHandle.size() == viaReadAll.size());
  FSC_CHECK(viaHandle == viaReadAll);
  FSC_CHECK(viaHandle == body);
  FSC_CHECK(h->position() == h->size());
}

// A partial read gives back what it got and moves position() by exactly that,
// not by what was asked for.
inline void handlePartialReadAdvances(FileSystem& fs, FsContractReport& r) {
  const std::string body = fsPatternBody(100);
  FSC_REQUIRE(fs.writeAll("/h/partial.bin", body));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/partial.bin");
  FSC_REQUIRE(h != nullptr);

  char buf[16] = {0};
  FSC_CHECK(h->read(buf, 10) == 10u);
  FSC_CHECK(h->position() == 10u);
  FSC_CHECK(std::string(buf, 10) == body.substr(0, 10));

  FSC_CHECK(h->read(buf, 5) == 5u);
  FSC_CHECK(h->position() == 15u);
  FSC_CHECK(std::string(buf, 5) == body.substr(10, 5));

  // Asked for more than is left: 10 bytes, not 16, and position lands on the end
  // rather than past it.
  FSC_REQUIRE(h->seek(90));
  FSC_CHECK(h->read(buf, sizeof(buf)) == 10u);
  FSC_CHECK(h->position() == 100u);
  FSC_CHECK(std::string(buf, 10) == body.substr(90, 10));
}

// At the end there is nothing to be short of, so 0 is the answer and not a
// failure. It must also be a STABLE answer -- asking twice must not sour the
// handle, which is exactly what an istream's sticky failbit would do.
inline void handleReadPastEndIsZeroNotAnError(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/h/end.bin", "abcde"));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/end.bin");
  FSC_REQUIRE(h != nullptr);

  char buf[8] = {0};
  FSC_CHECK(h->read(buf, sizeof(buf)) == 5u);
  FSC_CHECK(h->position() == 5u);
  FSC_CHECK(h->read(buf, sizeof(buf)) == 0u);
  FSC_CHECK(h->position() == 5u);
  FSC_CHECK(h->read(buf, sizeof(buf)) == 0u);
  FSC_CHECK(h->position() == 5u);

  // ...and the handle is still good afterwards: a read at the end must not have
  // closed the door on the bytes that are still there.
  FSC_REQUIRE(h->seek(1));
  FSC_CHECK(h->read(buf, 2) == 2u);
  FSC_CHECK(std::string(buf, 2) == "bc");
}

// PINNED DECISION: seek past the end is REFUSED, and position() does not move.
// seek(size()) is legal -- a zip reader's end-of-central-directory scan seeks
// deliberately close to the end, so "that offset does not exist" and "I am at
// the end" have to be different answers. See FileHandle in filesystem.h.
inline void handleSeekPastTheEndIsRefused(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/h/seek.bin", "0123456789"));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/seek.bin");
  FSC_REQUIRE(h != nullptr);
  FSC_REQUIRE(h->size() == 10u);

  FSC_REQUIRE(h->seek(4));
  FSC_CHECK(h->position() == 4u);

  FSC_CHECK(!h->seek(11));
  FSC_CHECK(h->position() == 4u);  // refused, so it did not move
  FSC_CHECK(!h->seek(0xFFFFFFFFu));
  FSC_CHECK(h->position() == 4u);

  // A refused seek did not damage the handle: the next read is the one that was
  // going to happen anyway.
  char buf[4] = {0};
  FSC_CHECK(h->read(buf, 2) == 2u);
  FSC_CHECK(std::string(buf, 2) == "45");

  // Exactly the end is legal, and reads nothing.
  FSC_CHECK(h->seek(10));
  FSC_CHECK(h->position() == 10u);
  FSC_CHECK(h->read(buf, sizeof(buf)) == 0u);

  FSC_CHECK(h->seek(0));
  FSC_CHECK(h->position() == 0u);
}

// Forwards, backwards, and back to where it already was. Backwards is the one
// that matters: SdFat has to walk the cluster chain from the start again, and a
// forward-only implementation would pass every other clause here.
inline void handleInterleavedSeekAndRead(FileSystem& fs, FsContractReport& r) {
  const std::string body = fsPatternBody(1500);
  FSC_REQUIRE(fs.writeAll("/h/inter.bin", body));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/inter.bin");
  FSC_REQUIRE(h != nullptr);

  // Offsets chosen to cross a 512-byte sector boundary in both directions and to
  // revisit one already read, since a handle that cached a sector and forgot to
  // invalidate it reads the right bytes only the first time.
  const uint32_t offsets[] = {1000, 4, 511, 1499, 512, 4, 1000, 0};
  for (uint32_t off : offsets) {
    FSC_REQUIRE(h->seek(off));
    FSC_CHECK(h->position() == off);
    char buf[6] = {0};
    const size_t want = body.size() - off < sizeof(buf) ? body.size() - off : sizeof(buf);
    FSC_CHECK(h->read(buf, sizeof(buf)) == want);
    FSC_CHECK(std::string(buf, want) == body.substr(off, want));
    FSC_CHECK(h->position() == off + want);
  }
}

// Zero bytes is a no-op, not an error and not an end-of-file. A caller looping
// over a chunk size that happens to reach zero must not be told the file ended.
inline void handleZeroLengthReadIsANoOp(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/h/zero.bin", "abcdef"));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/zero.bin");
  FSC_REQUIRE(h != nullptr);

  char buf[4] = {'!', '!', '!', '!'};
  FSC_CHECK(h->read(buf, 0) == 0u);
  FSC_CHECK(h->position() == 0u);
  FSC_CHECK(buf[0] == '!');  // and it did not write into the buffer
  // `dst` is not dereferenced for a zero count, so null is allowed.
  FSC_CHECK(h->read(nullptr, 0) == 0u);
  FSC_CHECK(h->position() == 0u);

  FSC_CHECK(h->read(buf, 3) == 3u);
  FSC_CHECK(std::string(buf, 3) == "abc");
  FSC_CHECK(h->read(buf, 0) == 0u);
  FSC_CHECK(h->position() == 3u);  // still where it was, mid-file
  FSC_CHECK(h->read(buf, 3) == 3u);
  FSC_CHECK(std::string(buf, 3) == "def");
}

// The two ways an open must fail, plus the one that is easy to get wrong: a
// directory opens perfectly well on every one of these backends, and only an
// explicit check refuses it.
inline void handleOpenRefusesDirectoryAndMissing(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/h/adir"));
  FSC_CHECK(fs.openRead("/h/adir") == nullptr);
  FSC_CHECK(fs.openRead("/") == nullptr);  // the root is a directory too
  FSC_CHECK(fs.openRead("/h/absent.bin") == nullptr);
  FSC_CHECK(fs.openRead("/no/such/dir/absent.bin") == nullptr);
  FSC_CHECK(fs.openRead("") == nullptr);  // normalises to "/", which is a directory
  // A file that WAS there and is not any more.
  FSC_REQUIRE(fs.writeAll("/h/gone.bin", "x"));
  FSC_REQUIRE(fs.remove("/h/gone.bin"));
  FSC_CHECK(fs.openRead("/h/gone.bin") == nullptr);
}

// TWO HANDLES AT ONCE, and this is not hypothetical: a zip reader holds the
// archive open while it reads an entry out of it, so the day this fails is the
// day the reader cannot open a book. Each handle carries its own position, and
// neither may be disturbed by the other's seeks -- SdFat's ONE 512-byte sector
// cache is shared between them, so two handles interleaved in the same file is
// the case where a cached sector gets used for the wrong reader.
inline void handleTwoOpenAtOnce(FileSystem& fs, FsContractReport& r) {
  const std::string body = fsPatternBody(1200);
  FSC_REQUIRE(fs.writeAll("/h/one.bin", body));
  FSC_REQUIRE(fs.writeAll("/h/two.bin", "SECOND FILE"));

  std::unique_ptr<FileHandle> a = fs.openRead("/h/one.bin");
  std::unique_ptr<FileHandle> b = fs.openRead("/h/two.bin");
  FSC_REQUIRE(a != nullptr);
  FSC_REQUIRE(b != nullptr);
  FSC_CHECK(a->size() == body.size());
  FSC_CHECK(b->size() == 11u);

  char ba[8] = {0};
  char bb[8] = {0};
  FSC_CHECK(a->read(ba, 6) == 6u);
  FSC_CHECK(std::string(ba, 6) == body.substr(0, 6));
  FSC_CHECK(b->read(bb, 6) == 6u);
  FSC_CHECK(std::string(bb, 6) == "SECOND");
  // a's position survived b's read...
  FSC_CHECK(a->position() == 6u);
  FSC_CHECK(a->read(ba, 6) == 6u);
  FSC_CHECK(std::string(ba, 6) == body.substr(6, 6));
  // ...and b's survived a's.
  FSC_CHECK(b->position() == 6u);
  FSC_CHECK(b->read(bb, 5) == 5u);
  FSC_CHECK(std::string(bb, 5) == " FILE");

  // Two handles on the SAME file, seeking against each other.
  std::unique_ptr<FileHandle> c = fs.openRead("/h/one.bin");
  FSC_REQUIRE(c != nullptr);
  FSC_REQUIRE(c->seek(1000));
  FSC_REQUIRE(a->seek(100));
  FSC_CHECK(c->position() == 1000u);
  FSC_CHECK(c->read(ba, 8) == 8u);
  FSC_CHECK(std::string(ba, 8) == body.substr(1000, 8));
  FSC_CHECK(a->position() == 100u);
  FSC_CHECK(a->read(ba, 8) == 8u);
  FSC_CHECK(std::string(ba, 8) == body.substr(100, 8));

  // Closing one leaves the other usable -- the underlying file objects are
  // separate, and a close that took the volume's cache with it would show here.
  c.reset();
  FSC_CHECK(a->read(ba, 8) == 8u);
  FSC_CHECK(std::string(ba, 8) == body.substr(108, 8));
}

// An empty file is a file, and a handle on it is a valid handle with nothing in
// it -- not a failed open. "/.reader/settings.json before anything wrote it" is
// a real instance of this.
inline void handleOnAnEmptyFile(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/h/empty.bin", ""));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/empty.bin");
  FSC_REQUIRE(h != nullptr);
  FSC_CHECK(h->size() == 0u);
  FSC_CHECK(h->position() == 0u);

  char buf[4] = {'!', '!', '!', '!'};
  FSC_CHECK(h->read(buf, sizeof(buf)) == 0u);
  FSC_CHECK(h->position() == 0u);
  FSC_CHECK(buf[0] == '!');

  FSC_CHECK(h->seek(0));  // 0 == size(), so it is the one legal offset
  FSC_CHECK(h->position() == 0u);
  FSC_CHECK(!h->seek(1));
  FSC_CHECK(h->position() == 0u);
}

// The whole reason the handle exists: a file bigger than the buffer reading it,
// reassembled byte-exact. 4300 bytes over 250-byte chunks is deliberate -- 250
// divides neither 512 nor 4300, so every chunk after the first starts mid-sector
// and the last one is short, which is where an off-by-one lives.
inline void handleLargeFileInChunks(FileSystem& fs, FsContractReport& r) {
  const std::string body = fsPatternBody(4300);
  FSC_REQUIRE(fs.writeAll("/h/large.bin", body));
  std::unique_ptr<FileHandle> h = fs.openRead("/h/large.bin");
  FSC_REQUIRE(h != nullptr);
  FSC_REQUIRE(h->size() == 4300u);

  const std::string got = fsDrain(*h, 250);
  FSC_CHECK(got.size() == body.size());
  FSC_CHECK(got == body);
  FSC_CHECK(h->position() == h->size());

  // The same file again from the tail, the way a zip reader reaches a central
  // directory: seek near the end, read the last stretch, then jump back.
  FSC_REQUIRE(h->seek(4300 - 300));
  const std::string tail = fsDrain(*h, 128);
  FSC_CHECK(tail.size() == 300u);
  FSC_CHECK(tail == body.substr(4000));
  FSC_REQUIRE(h->seek(0));
  char head[16] = {0};
  FSC_CHECK(h->read(head, sizeof(head)) == sizeof(head));
  FSC_CHECK(std::string(head, sizeof(head)) == body.substr(0, sizeof(head)));
}

// Nothing may succeed, and nothing may bring the storage into existence as a
// side effect. Runs against a filesystem whose mounted() is false, which is the
// one clause that does NOT want empty-and-mounted storage.
inline void unmounted(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(!fs.mounted());

  FSC_CHECK(!fs.exists("/anything"));
  FSC_CHECK(!fs.exists("/"));

  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  FSC_CHECK(!fs.list("/", out));
  FSC_CHECK(out.size() == 1);

  std::string got = "sentinel";
  FSC_CHECK(!fs.readAll("/anything", got));
  FSC_CHECK(got == "sentinel");

  // A handle is storage held open, so with no storage there is nothing to hold:
  // null, and not a handle that reads zeroes.
  FSC_CHECK(fs.openRead("/anything") == nullptr);
  FSC_CHECK(fs.openRead("/") == nullptr);

  FSC_CHECK(!fs.writeAll("/anything", "x"));
  FSC_CHECK(!fs.mkdirs("/anything"));
  // remove's end-state contract does NOT apply here: with no storage we cannot
  // know the file is gone, so claiming success would be a lie.
  FSC_CHECK(!fs.remove("/anything"));
}

}  // namespace fs_contract

#undef FSC_CHECK
#undef FSC_REQUIRE

// The clauses that need a mounted, empty filesystem, in the order they were
// written. `count` is set to the number returned.
inline const FsContractClause* fsContractClauses(size_t& count) {
  static const FsContractClause kClauses[] = {
      {"a written file exists and reads back byte-identical", &fs_contract::roundTrip},
      {"embedded newlines and NULs survive the round trip", &fs_contract::binarySafe},
      {"an empty file is a file, not an absence", &fs_contract::emptyFileIsAFile},
      {"writeAll truncates rather than appending", &fs_contract::writeTruncates},
      {"writeAll creates missing parents", &fs_contract::writeCreatesParents},
      {"list appends rather than clearing, and reports isDir and size",
       &fs_contract::listAppendsAndReports},
      {"list names leaves, not paths", &fs_contract::listNamesLeaves},
      {"list on a file, or on a missing path, is false and leaves out alone",
       &fs_contract::listRefusesFileAndMissing},
      {"the root is a listable directory", &fs_contract::rootIsListable},
      {"readAll on a missing file is false and leaves out untouched",
       &fs_contract::readAllMissingLeavesOut},
      {"readAll on a directory is false", &fs_contract::readAllRefusesDirectory},
      {"remove is about the end state", &fs_contract::removeIsAboutTheEndState},
      {"remove refuses a directory", &fs_contract::removeRefusesDirectory},
      {"mkdirs is idempotent and creates the whole chain", &fs_contract::mkdirsIsIdempotent},
      {"a file cannot be a parent directory", &fs_contract::fileCannotBeAParent},
      {"writeAll refuses a path that is a directory", &fs_contract::writeRefusesDirectory},
      {"a redundant or trailing separator addresses the same thing",
       &fs_contract::separatorsNormalise},
      // openRead / FileHandle -- the EPUB path.
      {"a full read through a handle matches readAll byte for byte",
       &fs_contract::handleFullReadMatchesReadAll},
      {"a partial read returns what it got and advances position",
       &fs_contract::handlePartialReadAdvances},
      {"reading at or past the end returns 0 and is not an error",
       &fs_contract::handleReadPastEndIsZeroNotAnError},
      {"seeking past the end is refused and does not move position",
       &fs_contract::handleSeekPastTheEndIsRefused},
      {"interleaved seek and read land on the right bytes, backwards included",
       &fs_contract::handleInterleavedSeekAndRead},
      {"a zero-length read is a no-op, not an error", &fs_contract::handleZeroLengthReadIsANoOp},
      {"openRead refuses a directory and a missing file",
       &fs_contract::handleOpenRefusesDirectoryAndMissing},
      {"two handles are open at once and neither disturbs the other",
       &fs_contract::handleTwoOpenAtOnce},
      {"a handle on an empty file has size 0 and reads nothing",
       &fs_contract::handleOnAnEmptyFile},
      {"a file larger than the buffer reassembles exactly, in chunks",
       &fs_contract::handleLargeFileInChunks},
  };
  count = sizeof(kClauses) / sizeof(kClauses[0]);
  return kClauses;
}

// The single clause for a filesystem with no storage behind it. Run it with
// `clause.run(fs, r)` directly, NOT through fsRunClause below -- its whole point
// is a filesystem whose mounted() is false, which is the one thing fsRunClause
// refuses.
inline const FsContractClause& fsUnmountedClause() {
  static const FsContractClause kClause{"with mounted() false, nothing succeeds",
                                        &fs_contract::unmounted};
  return kClause;
}

// Runs one clause. The mounted() precondition is asserted here rather than in
// each clause so that both runners agree on what "handed fresh storage" means,
// and so a runner that forgot to create its scratch directory fails loudly
// instead of reporting twenty-seven unrelated failures.
inline void fsRunClause(const FsContractClause& clause, FileSystem& fs, FsContractReport& r) {
  if (!r.require(fs.mounted(), "fs.mounted()")) return;
  clause.run(fs, r);
}

}  // namespace reader
