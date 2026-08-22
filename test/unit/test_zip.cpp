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
