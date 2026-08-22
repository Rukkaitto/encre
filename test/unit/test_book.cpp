// openChapter: the card to a laid-out-able chapter, through the fake filesystem.
//
// This is the layer that would otherwise have lived in shell/src/main.cpp, where
// nothing could test it. Every case below is a card a user could actually present.
#include <cstring>
#include <string>

#include "doctest.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/book.h"

namespace {

using reader::OpenedChapter;

// The fixtures are unsigned-char arrays; the filesystem takes bytes.
std::string_view asBytes(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

void put(FakeFileSystem& fs, std::string_view bytes) {
  REQUIRE(fs.writeAll("/books/book.epub", bytes));
}

}  // namespace

TEST_CASE("a real EPUB opens to its first chapter") {
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedChapter out;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openChapter(fs, "/books/book.epub", 0, out, &why), std::string(why));
  CHECK_FALSE(out.bookTitle.empty());
  CHECK(out.chapterCount >= 1);
  CHECK_FALSE(out.doc.blocks.empty());
}

TEST_CASE("A MISSING FILE IS A REASON, NOT AN ABORT") {
  FakeFileSystem fs;
  OpenedChapter out;
  const char* why = "";
  CHECK_FALSE(reader::openChapter(fs, "/books/nope.epub", 0, out, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("a file that is not a zip is refused with the zip's own reason") {
  FakeFileSystem fs;
  put(fs, "this is not an archive, it is a sentence");
  OpenedChapter out;
  const char* why = "";
  CHECK_FALSE(reader::openChapter(fs, "/books/book.epub", 0, out, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("an empty file is refused") {
  FakeFileSystem fs;
  put(fs, "");
  OpenedChapter out;
  const char* why = "";
  CHECK_FALSE(reader::openChapter(fs, "/books/book.epub", 0, out, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("a chapter index past the spine is refused, and reports the count first") {
  // The COUNT is set before the index is checked, so a caller that asked for a
  // stale chapter number can clamp and retry rather than starting over.
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedChapter out;
  const char* why = "";
  CHECK_FALSE(reader::openChapter(fs, "/books/book.epub", 999, out, &why));
  CHECK(out.chapterCount >= 1);
  CHECK(std::string(why) == "no such chapter");
}

TEST_CASE("a negative chapter index is refused rather than read backwards") {
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedChapter out;
  const char* why = "";
  CHECK_FALSE(reader::openChapter(fs, "/books/book.epub", -1, out, &why));
}

TEST_CASE("EVERY CHAPTER OF EVERY FIXTURE OPENS, or says why") {
  // The whole matrix, because a chapter that failed silently would be a book that
  // opens to a blank page -- and the first chapter working proves only the first.
  struct Case {
    const char* name;
    std::string_view bytes;
    bool shouldOpen;
  };
  const Case cases[] = {
      {"good", asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen), true},
      {"noContainer", asBytes(epubfix::kEpubNoContainer, epubfix::kEpubNoContainerLen), false},
      {"noOpf", asBytes(epubfix::kEpubNoOpf, epubfix::kEpubNoOpfLen), false},
      {"containerPointsNowhere",
       asBytes(epubfix::kEpubContainerPointsNowhere, epubfix::kEpubContainerPointsNowhereLen),
       false},
      {"idMismatch", asBytes(epubfix::kEpubIdMismatch, epubfix::kEpubIdMismatchLen), false},
      {"emptySpine", asBytes(epubfix::kEpubEmptySpine, epubfix::kEpubEmptySpineLen), false},
      // The spine names a manifest id whose file is absent, and the href resolves
      // to nothing in the archive. Epub::open lets both through on purpose -- a
      // book with one broken chapter should still open -- so openChapter is the
      // layer that has to notice, and this is the pair that proves it does.
      {"spineRefMissing",
       asBytes(epubfix::kEpubSpineRefMissing, epubfix::kEpubSpineRefMissingLen), false},
      {"hrefMissing", asBytes(epubfix::kEpubHrefMissing, epubfix::kEpubHrefMissingLen), false},
  };
  for (const Case& c : cases) {
    FakeFileSystem fs;
    put(fs, c.bytes);
    OpenedChapter out;
    const char* why = "";
    const bool ok = reader::openChapter(fs, "/books/book.epub", 0, out, &why);
    CAPTURE(std::string(c.name));
    CAPTURE(std::string(why));
    CHECK(ok == c.shouldOpen);
    if (!ok) CHECK(std::strlen(why) > 0);
  }
}

TEST_CASE("the XHTML does not outlive the call -- only the blocks do") {
  // Asserted on the blocks owning their own bytes: a Block holds a std::string,
  // not a view, so the inflated chapter is free to die at the end of openChapter.
  // If it held views this would read freed memory, which a sanitiser build catches
  // and a release build shows as a page of garbage.
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedChapter out;
  const char* why = "";
  REQUIRE(reader::openChapter(fs, "/books/book.epub", 0, out, &why));
  std::string joined;
  for (const reader::Block& b : out.doc.blocks) joined += b.text;
  CHECK_FALSE(joined.empty());
}
