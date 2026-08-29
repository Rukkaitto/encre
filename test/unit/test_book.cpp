// openBook and ChapterReader: the card to blocks you can lay out, streamed.
//
// This is the layer that would otherwise have lived in shell/src/main.cpp, where
// nothing could test it. Every case below is a card a user could actually present.
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "epub_builder.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/epub.h"

namespace {

using reader::OpenedBook;

// The fixtures are unsigned-char arrays; the filesystem takes bytes.
std::string_view asBytes(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

void put(FakeFileSystem& fs, std::string_view bytes) {
  REQUIRE(fs.writeAll("/books/book.epub", bytes));
}

// Every block of a chapter, streamed. The `[kind]text` form so a whole chapter is
// one assertion.
std::string streamChapter(FakeFileSystem& fs, const reader::ChapterLocation& where,
                          const char** why) {
  reader::ChapterReader r;
  if (!r.begin(fs, where)) {
    *why = r.error();
    return {};
  }
  std::string out;
  reader::Block b;
  while (r.next(b)) {
    out += "[" + b.text + "]";
    b = reader::Block{};  // dropped, as the reader's caller drops it
  }
  *why = r.error();
  return out;
}

}  // namespace

TEST_CASE("a real EPUB opens to a location, and the location streams to blocks") {
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", out, &why), std::string(why));
  CHECK_FALSE(out.title.empty());
  CHECK(out.chapterCount() >= 1);
  // What comes back is a LOCATION, not a chapter: three numbers and a path.
  CHECK(out.path == "/books/book.epub");
  CHECK(out.locate(0).compressedSize > 0);

  const char* streamWhy = "";
  const std::string blocks = streamChapter(fs, out.locate(0), &streamWhy);
  CHECK(std::strlen(streamWhy) == 0);
  CHECK_FALSE(blocks.empty());
}

TEST_CASE("THE STREAMED BLOCKS ARE THE ONES buildDocument GIVES") {
  // The two paths through the same chapter -- one over a buffer, one over an
  // inflating stream -- must agree exactly. This is the cross-check that catches a
  // resumption bug in any of the four layers below it, because a buffer parse has
  // no resumption at all.
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));

  // The buffer path: read the entry whole, parse it whole.
  std::unique_ptr<reader::FileHandle> file = fs.openRead("/books/book.epub");
  REQUIRE(file != nullptr);
  reader::Zip zip;
  REQUIRE(zip.open(*file));
  reader::Epub book;
  REQUIRE(book.open(*file, zip));
  const reader::Zip::Entry* e = zip.find(book.chapters()[0].path);
  REQUIRE(e != nullptr);
  std::string xhtml;
  REQUIRE(zip.read(*file, *e, xhtml));
  reader::Document whole;
  REQUIRE(reader::buildDocument(xhtml, whole, &why));
  std::string viaBuffer;
  for (const reader::Block& b : whole.blocks) viaBuffer += "[" + b.text + "]";

  const char* streamWhy = "";
  CHECK(streamChapter(fs, out.locate(0), &streamWhy) == viaBuffer);
  CHECK_FALSE(viaBuffer.empty());
}

TEST_CASE("REWIND GIVES THE SAME BLOCKS AGAIN, which is what a backward turn costs") {
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));

  reader::ChapterReader r;
  REQUIRE(r.begin(fs, out.locate(0)));
  const auto drain = [&]() {
    std::string s;
    reader::Block b;
    while (r.next(b)) s += "[" + b.text + "]";
    return s;
  };
  const std::string first = drain();
  REQUIRE_FALSE(first.empty());
  CHECK(r.position() > 0);

  REQUIRE(r.rewind());
  CHECK(r.position() == 0);
  CHECK(drain() == first);
  // And again, because a rewind reuses every buffer and the second one is where a
  // half-reset shows up.
  REQUIRE(r.rewind());
  CHECK(drain() == first);
}

TEST_CASE("a rewind partway through still restarts at the first block") {
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));

  reader::ChapterReader r;
  REQUIRE(r.begin(fs, out.locate(0)));
  reader::Block first;
  REQUIRE(r.next(first));
  reader::Block second;
  const bool hadSecond = r.next(second);

  REQUIRE(r.rewind());
  reader::Block again;
  REQUIRE(r.next(again));
  CHECK(again.text == first.text);
  CHECK(again.kind == first.kind);
  if (hadSecond) {
    reader::Block again2;
    REQUIRE(r.next(again2));
    CHECK(again2.text == second.text);
  }
}

// --- Refusals ----------------------------------------------------------------

TEST_CASE("A MISSING FILE IS A REASON, NOT AN ABORT") {
  FakeFileSystem fs;
  OpenedBook out;
  const char* why = "";
  CHECK_FALSE(reader::openBook(fs, "/books/nope.epub", out, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("a file that is not a zip is refused with the zip's own reason") {
  FakeFileSystem fs;
  put(fs, "this is not an archive, it is a sentence");
  OpenedBook out;
  const char* why = "";
  CHECK_FALSE(reader::openBook(fs, "/books/book.epub", out, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("an empty file is refused") {
  FakeFileSystem fs;
  put(fs, "");
  OpenedBook out;
  const char* why = "";
  CHECK_FALSE(reader::openBook(fs, "/books/book.epub", out, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("AN OUT-OF-RANGE CHAPTER YIELDS AN UNUSABLE LOCATION, not a bad read") {
  // openBook takes no chapter index any more -- it reads the whole spine's offsets
  // once, because calling it per chapter re-parsed the central directory and the OPF
  // every time (~32 KB transient, three times over, just to reach this book's first
  // chapter with text). So the range check lives on locate(), and its refusal is a
  // location with no size, which ChapterReader declines.
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));
  REQUIRE(out.chapterCount() >= 1);

  for (const int bad : {-1, 999, out.chapterCount()}) {
    CAPTURE(bad);
    const reader::ChapterLocation where = out.locate(bad);
    CHECK(where.compressedSize == 0);
    CHECK(where.bookPath.empty());
    reader::ChapterReader r;
    CHECK_FALSE(r.begin(fs, where));
  }
  // And a valid one is usable.
  CHECK(out.locate(0).compressedSize > 0);
}

TEST_CASE("THE WHOLE SPINE IS LOCATED IN ONE CALL") {
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));
  // Every entry has a span, and each readable one addresses distinct bytes.
  CHECK(out.chapters.size() == static_cast<size_t>(out.chapterCount()));
  for (int i = 0; i < out.chapterCount(); ++i) {
    CAPTURE(i);
    const reader::ChapterSpan& c = out.chapters[static_cast<size_t>(i)];
    if (!c.readable()) continue;
    CHECK(c.localHeaderOffset > 0);
    const char* w = "";
    const std::string blocks = streamChapter(fs, out.locate(i), &w);
    CHECK(std::strlen(w) == 0);
  }
}

TEST_CASE("EVERY EPUB FIXTURE OPENS OR SAYS WHY") {
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
      // BOTH IDENTIFIER SHAPES OPEN. An OPF whose `unique-identifier` names an id no
      // dc:identifier carries is a book with unusable metadata, not an unreadable
      // book -- 4 of 16 EPUBs in one real library are one of these two shapes, and
      // every one of them reads. It was a refusal until a device report; epub.cpp
      // step 3 carries the whole reasoning.
      {"idMismatch", asBytes(epubfix::kEpubIdMismatch, epubfix::kEpubIdMismatchLen), true},
      {"noUniqueId", asBytes(epubfix::kEpubNoUniqueId, epubfix::kEpubNoUniqueIdLen), true},
      {"emptySpine", asBytes(epubfix::kEpubEmptySpine, epubfix::kEpubEmptySpineLen), false},
      // The spine names a manifest id whose file is absent, and the href resolves
      // to nothing in the archive. Epub::open lets both through on purpose -- a
      // book with one broken chapter should still open -- so openBook is the layer
      // that has to notice, and this is the pair that proves it does.
      // BOTH REFUSED, BY Epub::open, with two distinct messages -- "the spine
      // references an item the manifest does not list" and "a spine item's file is
      // not in the archive" (epub.cpp:186 and :189). It validates every spine entry
      // against the archive before returning.
      //
      // This pair was briefly expected to OPEN, on a claim that Epub::open lets a
      // broken chapter through so a book with one still opens. That claim was in a
      // comment in book.cpp and it was never true of the code; asserting it here is
      // what finally read epub.cpp. A comment about a neighbouring layer is not
      // evidence about it.
      {"spineRefMissing",
       asBytes(epubfix::kEpubSpineRefMissing, epubfix::kEpubSpineRefMissingLen), false},
      {"hrefMissing", asBytes(epubfix::kEpubHrefMissing, epubfix::kEpubHrefMissingLen), false},
  };
  for (const Case& c : cases) {
    FakeFileSystem fs;
    put(fs, c.bytes);
    OpenedBook out;
    const char* why = "";
    const bool ok = reader::openBook(fs, "/books/book.epub", out, &why);
    CAPTURE(std::string(c.name));
    CAPTURE(std::string(why));
    CHECK(ok == c.shouldOpen);
    if (!ok) CHECK(std::strlen(why) > 0);
  }
}

TEST_CASE("a chapter reader over a location that no longer opens fails cleanly") {
  // The card was pulled between opening the book and reading the chapter, which on
  // this device is a real sequence: the location survives, the file does not.
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));

  FakeFileSystem empty;
  reader::ChapterReader r;
  CHECK_FALSE(r.begin(empty, out.locate(0)));
  CHECK(std::strlen(r.error()) > 0);
  reader::Block b;
  CHECK_FALSE(r.next(b));  // and stays refused rather than reading from nothing
}


TEST_CASE("AN UNREADABLE SPAN IS A LOCAL-HEADER FAILURE, not a missing file") {
  // ChapterSpan::readable() exists for the one case that reaches it: zip.locate()
  // failing on an entry whose local header is corrupt or whose data runs past the
  // end of the file. A file the archive simply does not contain never gets here --
  // Epub::open refuses the whole book first (see the matrix above).
  //
  // So this asserts the shape rather than manufacturing the case: every span of a
  // good book is readable, and locate() refuses out of range.
  FakeFileSystem fs;
  put(fs, asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  OpenedBook out;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", out, &why));
  for (const reader::ChapterSpan& c : out.chapters) CHECK(c.readable());
  CHECK_FALSE(reader::ChapterSpan{}.readable());
}


// --- WHERE THE COVER IS ---------------------------------------------------------

TEST_CASE("openBook records where the cover is, by both OPF routes") {
  // Route 1: <meta name="cover" content="id">, the EPUB 2 convention -- and the one
  // the 225-book corpus overwhelmingly uses. Route 2: a manifest item with
  // properties="cover-image", EPUB 3's. Both are needed: real files use one or the
  // other and NEITHER is required by any spec.
  //
  // The sizes are asserted, not just readable(), because readable() is only
  // `compressedSize > 0` -- which any entry in the archive satisfies. What says the
  // span points at the COVER and not at chapter one is its length and its storage
  // method: the fixture stores the image (method 0) as a real EPUB stores a JPEG.
  const size_t coverLen = std::strlen(epubbuild::kFakeCoverBytes);

  FakeFileSystem fs;
  put(fs, epubbuild::withCoverMetaTag());
  OpenedBook book;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", book, &why), std::string(why));
  CHECK(book.cover.readable());
  CHECK(book.cover.compressedSize == coverLen);
  CHECK(book.cover.uncompressedSize == coverLen);
  CHECK_FALSE(book.cover.deflated);

  FakeFileSystem fs3;
  put(fs3, epubbuild::withCoverProperties());
  OpenedBook book3;
  REQUIRE_MESSAGE(reader::openBook(fs3, "/books/book.epub", book3, &why), std::string(why));
  CHECK(book3.cover.readable());
  CHECK(book3.cover.uncompressedSize == coverLen);
  CHECK_FALSE(book3.cover.deflated);
  // NOT an offset comparison between the two books: they are two archives whose OPFs
  // differ in length, so the same entry legitimately sits at a different offset in
  // each. What has to agree is the ENTRY chosen, and its size and storage method say
  // that -- the only other entry in either archive that is stored is the mimetype,
  // which is a different length.
}

TEST_CASE("the cover's href resolves against the OPF's directory, as a spine href does") {
  // The fixture puts the cover at OEBPS/images/cover.jpg and the OPF says
  // `images/cover.jpg`. An implementation that took the href as an archive path
  // would find nothing; one that ignored the OPF's directory would look for
  // `images/cover.jpg` and also find nothing. Only resolveHref gets there, which is
  // the point: this is the SAME resolver a spine href goes through, not a second one.
  FakeFileSystem fs;
  put(fs, epubbuild::withCoverMetaTag());
  OpenedBook book;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", book, &why), std::string(why));
  REQUIRE(book.cover.readable());

  // locateCover mirrors locate(): the same four facts plus the book's own path.
  const reader::ChapterLocation at = book.locateCover();
  CHECK(at.bookPath == "/books/book.epub");
  CHECK(at.localHeaderOffset == book.cover.localHeaderOffset);
  CHECK(at.compressedSize == book.cover.compressedSize);
  CHECK(at.uncompressedSize == book.cover.uncompressedSize);
  CHECK(at.deflated == book.cover.deflated);
  // ...and it is not one of the chapters, which is the other way to be wrong.
  for (int i = 0; i < book.chapterCount(); ++i)
    CHECK(book.cover.localHeaderOffset != book.locate(i).localHeaderOffset);
}

TEST_CASE("a book with no cover opens normally and says it has none") {
  // 225 of 225 corpus books declare one, but the firmware must not require it: a
  // book that opens and reads is worth more than a cover, and the sleep screen has a
  // card to fall back to.
  FakeFileSystem fs;
  put(fs, epubbuild::minimalEpub());
  OpenedBook book;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", book, &why), std::string(why));
  CHECK(book.chapterCount() == 2);
  CHECK_FALSE(book.cover.readable());
  // An unreadable span yields a location with no size, exactly as locate() does for
  // an index out of range -- so a decoder refuses it without a special case.
  CHECK(book.locateCover().compressedSize == 0);
  CHECK(book.locateCover().bookPath.empty());
}

TEST_CASE("an EPUB 3 properties list is matched token by token, never as a substring") {
  // `properties` is a SPACE-SEPARATED SET, so `not-cover-image` contains the token
  // this code looks for and declares none of it. A find() over the attribute passes
  // every other test in this file and adopts this book's image as its cover.
  FakeFileSystem fs;
  put(fs, epubbuild::withCoverPropertiesLookalike());
  OpenedBook book;
  const char* why = "";
  REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", book, &why), std::string(why));
  CHECK_FALSE(book.cover.readable());
}
