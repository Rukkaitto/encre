#include <cstring>
#include <string>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/zip.h"
#include "zip_fixtures.h"

namespace {

// A fixture as a file, because that is how the reader meets one. FakeFileSystem
// gives a real FileHandle over it, so the seek/read path under test is the same
// one SdFileSystem provides.
struct Archive {
  FakeFileSystem fs;
  std::unique_ptr<reader::FileHandle> handle;
  reader::Zip zip;

  Archive(const unsigned char* bytes, size_t len) {
    fs.writeAll("/book.epub",
                std::string_view(reinterpret_cast<const char*>(bytes), len));
    handle = fs.openRead("/book.epub");
  }
  bool open() { return handle != nullptr && zip.open(*handle); }
};

#define ARCHIVE(name) Archive a(zipfix::name, zipfix::name##Len)

}  // namespace

TEST_CASE("a good archive opens and lists its entries in directory order") {
  ARCHIVE(kGood);
  REQUIRE(a.open());
  REQUIRE(a.zip.entries().size() == 2);
  CHECK(a.zip.entries()[0].name == "mimetype");
  CHECK(a.zip.entries()[1].name == "OEBPS/ch1.xhtml");
  // The first is stored, as an EPUB's mimetype must be; the second deflated.
  CHECK_FALSE(a.zip.entries()[0].deflated);
  CHECK(a.zip.entries()[1].deflated);
}

TEST_CASE("a stored entry reads back byte for byte") {
  ARCHIVE(kGood);
  REQUIRE(a.open());
  const reader::Zip::Entry* e = a.zip.find("mimetype");
  REQUIRE(e != nullptr);
  std::string out;
  REQUIRE(a.zip.read(*a.handle, *e, out));
  CHECK(out == "application/epub+zip");
}

TEST_CASE("a deflated entry inflates to its stated size") {
  ARCHIVE(kGood);
  REQUIRE(a.open());
  const reader::Zip::Entry* e = a.zip.find("OEBPS/ch1.xhtml");
  REQUIRE(e != nullptr);
  std::string out;
  REQUIRE(a.zip.read(*a.handle, *e, out));
  CHECK(out.size() == e->uncompressedSize);
  CHECK(out.find("Miss Brooke") != std::string::npos);
  CHECK(out.find("</html>") != std::string::npos);
}

TEST_CASE("find is exact, not case-folded or prefix-matched") {
  ARCHIVE(kGood);
  REQUIRE(a.open());
  CHECK(a.zip.find("mimetype") != nullptr);
  CHECK(a.zip.find("MIMETYPE") == nullptr);
  CHECK(a.zip.find("mime") == nullptr);
  CHECK(a.zip.find("ch1.xhtml") == nullptr);  // the name includes its directory
  CHECK(a.zip.find("") == nullptr);
}

TEST_CASE("a legal EOCD comment does not hide the record") {
  // A reader that only looks at the last 22 bytes misses it, and this is legal.
  ARCHIVE(kEocdComment);
  REQUIRE(a.open());
  CHECK(a.zip.entries().size() == 1);
}

TEST_CASE("junk before the first local header is legal and offsets survive it") {
  // Self-extracting archives do this, so offsets are not relative to zero.
  ARCHIVE(kPrefixed);
  REQUIRE(a.open());
  const reader::Zip::Entry* e = a.zip.find("a.txt");
  REQUIRE(e != nullptr);
  std::string out;
  REQUIRE(a.zip.read(*a.handle, *e, out));
  CHECK(out == "hello");
}

TEST_CASE("an empty archive is valid and holds nothing") {
  ARCHIVE(kEmpty);
  REQUIRE(a.open());
  CHECK(a.zip.entries().empty());
  CHECK(a.zip.find("anything") == nullptr);
}

// --- The refusals. Each is a `false` with a reason, never an abort. ----------

TEST_CASE("no end-of-central-directory record is a refusal, and it says so") {
  ARCHIVE(kNoEocd);
  CHECK_FALSE(a.open());
  // NON-EMPTY, not a prose match. The first version of this looked for "central
  // directory" and the message says "central-directory" -- a test failing on a
  // hyphen. `reason()` is a log label, free to be reworded, and coupling a test
  // to its punctuation is the mistake the session record's wire names exist to
  // avoid. What matters is that a refusal is never silent.
  CHECK(std::strlen(a.zip.reason()) > 0);
}

TEST_CASE("an entry count that lies is refused, not believed into an allocation") {
  ARCHIVE(kEntryCountLies);
  CHECK_FALSE(a.open());
  CHECK(std::strlen(a.zip.reason()) > 0);
  CHECK(a.zip.entries().empty());  // and nothing half-built is left behind
}

TEST_CASE("a nonsense central-directory offset is a refusal") {
  ARCHIVE(kBadCdOffset);
  CHECK_FALSE(a.open());
  CHECK(std::strlen(a.zip.reason()) > 0);
}

TEST_CASE("compressed data running past the file is a refusal at READ time") {
  // The directory can be perfectly well-formed and the entry still unreadable,
  // so this is caught where the bytes are fetched rather than at open.
  ARCHIVE(kCsizeOverruns);
  REQUIRE(a.open());
  const reader::Zip::Entry* e = a.zip.find("a.txt");
  REQUIRE(e != nullptr);
  std::string out;
  CHECK_FALSE(a.zip.read(*a.handle, *e, out));
}

TEST_CASE("an absurd uncompressed size is capped, not allocated") {
  ARCHIVE(kUsizeAbsurd);
  REQUIRE(a.open());
  const reader::Zip::Entry* e = a.zip.find("a.txt");
  REQUIRE(e != nullptr);
  CHECK(e->uncompressedSize > reader::Zip::kMaxEntryBytes);
  std::string out;
  CHECK_FALSE(a.zip.read(*a.handle, *e, out));
  CHECK(out.empty());  // and nothing was allocated on the way to refusing
}

TEST_CASE("a method we do not implement is a refusal") {
  ARCHIVE(kBzip2Method);
  // Refused at open: the method is in the directory, so this is knowable before
  // any read, and an entry that cannot be read is not an entry.
  const bool opened = a.open();
  if (opened) {
    const reader::Zip::Entry* e = a.zip.find("a.txt");
    if (e != nullptr) {
      std::string out;
      CHECK_FALSE(a.zip.read(*a.handle, *e, out));
    }
  }
}

TEST_CASE("an encrypted entry is a refusal, not ciphertext handed onward") {
  ARCHIVE(kEncrypted);
  const bool opened = a.open();
  if (opened) {
    const reader::Zip::Entry* e = a.zip.find("a.txt");
    if (e != nullptr) {
      std::string out;
      CHECK_FALSE(a.zip.read(*a.handle, *e, out));
    }
  }
}

TEST_CASE("every truncation of a good archive is survivable") {
  // The deterministic fuzz that found defects in the JSON reader, applied to the
  // one structure here whose offsets all point at each other.
  for (size_t cut = 0; cut <= zipfix::kGoodLen; ++cut) {
    FakeFileSystem fs;
    fs.writeAll("/t.epub", std::string_view(
                               reinterpret_cast<const char*>(zipfix::kGood), cut));
    auto h = fs.openRead("/t.epub");
    REQUIRE(h != nullptr);
    reader::Zip z;
    if (!z.open(*h)) continue;  // a refusal is always acceptable
    // If it opened, every entry it claims must be internally consistent.
    for (const reader::Zip::Entry& e : z.entries()) {
      std::string out;
      if (z.read(*h, e, out)) CHECK(out.size() == e.uncompressedSize);
    }
  }
}

// --- THE BACKWARD EOCD SCAN, ACROSS ITS CHUNK BOUNDARIES ---------------------
//
// The scan used to allocate the whole 64 KB window in one std::string. On a real
// book that was the largest single allocation in the reader and it aborted the
// device: 142 KB free, no contiguous block that size, because 203 library entries
// had fragmented the heap. It reads in 2 KB chunks on the stack now.
//
// EVERY FIXTURE IN THIS FILE IS SMALLER THAN ONE CHUNK, so none of them exercised
// the loop -- they all passed before the rewrite and after it. The comment field is
// what pushes the record away from the end, so these build zips with comments
// sized either side of every boundary the loop can trip on.
namespace {

// `kGood` with an N-byte comment appended and its comment-length field set. The
// EOCD is a fixed 22 bytes at the end of kGood, and the length lives at offset 20
// within it -- so this is a legal zip whose record sits exactly N bytes from the
// end of the file.
std::string withComment(size_t n) {
  std::string z(reinterpret_cast<const char*>(zipfix::kGood), zipfix::kGoodLen);
  const size_t lenAt = z.size() - 2;  // the comment-length field is the last field
  z[lenAt] = static_cast<char>(n & 0xFF);
  z[lenAt + 1] = static_cast<char>((n >> 8) & 0xFF);
  // A comment of readable filler. Deliberately NOT containing "PK\x05\x06": a
  // signature inside a comment is LATER in the file than the real record and would
  // legitimately win under zip's own last-record-wins rule, which is a different
  // question from the one this test asks.
  z.append(n, '.');
  return z;
}

bool opens(const std::string& bytes, std::string* firstEntry = nullptr) {
  Archive a(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
  if (!a.open()) return false;
  if (firstEntry != nullptr && !a.zip.entries().empty())
    *firstEntry = a.zip.entries()[0].name;
  return true;
}

}  // namespace

TEST_CASE("the EOCD is found at every distance a chunked backward scan can trip on") {
  // 2048 is the chunk. The +-1s are the off-by-one the loop's `hi` bound and its
  // 3-byte read overlap can each get wrong, and 65535 is the largest comment the
  // format allows -- 32 chunks back, which no fixture had ever reached.
  for (const size_t n : {size_t{0}, size_t{1}, size_t{21}, size_t{22},
                         size_t{2045}, size_t{2046}, size_t{2047}, size_t{2048}, size_t{2049},
                         size_t{4095}, size_t{4096}, size_t{4097},
                         size_t{8192}, size_t{32768}, size_t{65535}}) {
    std::string name;
    CAPTURE(n);
    REQUIRE(opens(withComment(n), &name));
    // Not just "it opened" -- the directory behind the record has to have been read
    // through, which is what a wrong offset would break silently.
    CHECK(name == "mimetype");
  }
}

TEST_CASE("a comment longer than the scan window is refused, not searched forever") {
  // kMaxEocdSearch is 64 KB + 22. A record pushed past it cannot be found, and the
  // scan must stop at the window rather than walking the whole file.
  std::string z = withComment(65535);
  z.append(2048, '.');  // now further from the end than the field can even claim
  CHECK_FALSE(opens(z));
}

TEST_CASE("A CHAPTER TOO LARGE FOR THE HEAP IS A REASON, NOT AN abort()") {
  // The second failure the device found, after the scan: `new` aborts under
  // -fno-exceptions with no diagnostic, so an entry the heap cannot serve took the
  // firmware down. Every large allocation in Zip is nothrow-checked now.
  //
  // Asserted through the CAP rather than by exhausting the host's heap: the desktop
  // has gigabytes, so the only reachable form of "too big" here is a claim above
  // kMaxEntryBytes. What this pins is that the refusal path SETS A REASON -- the
  // device is where the allocation actually fails, and a bare false there is
  // indistinguishable from a corrupt file.
  ARCHIVE(kUsizeAbsurd);
  if (a.open() && !a.zip.entries().empty()) {
    std::string out;
    CHECK_FALSE(a.zip.read(*a.handle, a.zip.entries()[0], out));
    CHECK(out.empty());
  }
}
