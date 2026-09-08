// The table of contents: chapter names, and which spine entry each one names.
//
// Written after MEASURING four real books rather than after reading the spec, and the
// measurement changed the design: all four carry an EPUB 2 `toc.ncx` and NOT ONE has
// an EPUB 3 nav document, none nests, and two of the four name one file several times.
// The fixtures here are those shapes.
#include <string>
#include <vector>

#include "doctest.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/toc.h"

namespace {

std::string_view asBytes(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

FakeFileSystem cardWith(std::string_view bytes) {
  FakeFileSystem fs;
  fs.writeAll("/books/book.epub", bytes);
  return fs;
}

std::vector<reader::TocEntry> tocOf(FakeFileSystem& fs, bool* ok = nullptr,
                                    const char** why = nullptr) {
  std::vector<reader::TocEntry> toc;
  const char* reason = "";
  const bool good = reader::loadToc(fs, "/books/book.epub", toc, &reason);
  if (ok != nullptr) *ok = good;
  if (why != nullptr) *why = reason;
  return toc;
}

}  // namespace

TEST_CASE("an NCX named by the spine's toc attribute is read") {
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubToc, epubfix::kEpubTocLen));
  bool ok = false;
  const char* why = "";
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok, &why);
  REQUIRE_MESSAGE(ok, std::string(why));
  REQUIRE(toc.size() == 2);
  CHECK(toc[0].label == "Miss Brooke");
  CHECK(toc[0].spine == 0);
  CHECK(toc[1].label == "Weights");
  CHECK(toc[1].spine == 1);
}

TEST_CASE("an NCX found only by its media type is read too") {
  // The spine's `toc` attribute is OPTIONAL and real books omit it, so the media type
  // has to be enough on its own -- it is what makes an NCX an NCX.
  FakeFileSystem fs =
      cardWith(asBytes(epubfix::kEpubTocNoSpineAttr, epubfix::kEpubTocNoSpineAttrLen));
  bool ok = false;
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
  REQUIRE(ok);
  REQUIRE(toc.size() == 2);
  CHECK(toc[0].label == "Miss Brooke");
}

TEST_CASE("a book with NO table of contents is not a failure") {
  // It reads perfectly well; it just cannot name its chapters. Every caller's answer
  // is to fall back on the spine position it already shows, so refusing here would
  // turn a missing feature into a broken book.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen));
  bool ok = false;
  const char* why = "";
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok, &why);
  CHECK(ok);
  CHECK(toc.empty());
  CHECK(std::string(why).empty());
}

TEST_CASE("an identical row twice is dropped; two labels for one file are kept") {
  // BOTH SHAPES COME FROM ONE REAL BOOK. Le Fleau's NCX names spine 3 twice with the
  // same label -- two rows a reader cannot tell apart, going to the same place -- and
  // names spine 4 twice with different labels, which is the book describing two
  // sections of one file. Dropping the second kind would hide content; keeping the
  // first would show a duplicate row.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubTocRepeats, epubfix::kEpubTocRepeatsLen));
  bool ok = false;
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
  REQUIRE(ok);
  REQUIRE(toc.size() == 3);
  CHECK(toc[0].label == "Pour Tabby");   // the identical repeat is gone
  CHECK(toc[0].spine == 0);
  CHECK(toc[1].spine == 1);              // ...and both distinct labels remain
  CHECK(toc[2].spine == 1);
  CHECK(toc[1].label != toc[2].label);
  // UTF-8 survives: NCX labels are UTF-8 and the theme shouts them, so a mangled byte
  // here would surface as a wrong glyph on the Contents screen.
  CHECK(toc[1].label == "PREMI\xC3\x88RE PARTIE");
}

TEST_CASE("nested navPoints are flattened, not dropped") {
  // No measured book nests, so this proves the flattening rather than describing a
  // shape anyone ships -- an inner point is an entry at whatever depth it was authored.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubTocNested, epubfix::kEpubTocNestedLen));
  bool ok = false;
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
  REQUIRE(ok);
  CHECK(toc.size() == 4);  // two outer, two inner
  bool sawInner = false;
  for (const reader::TocEntry& e : toc)
    if (e.label.find("(inner)") != std::string::npos) sawInner = true;
  CHECK(sawInner);
}

TEST_CASE("an entry naming a file the spine does not read is skipped") {
  // An NCX may point at anything in the archive. A row that cannot be opened is worse
  // than a missing row, so it does not become one.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubTocOffSpine, epubfix::kEpubTocOffSpineLen));
  bool ok = false;
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
  REQUIRE(ok);
  REQUIRE(toc.size() == 1);  // `notes.xhtml` is not in the spine
  // ...and a fragment target resolves to the file that holds it.
  CHECK(toc[0].label == "Two, part two");
  CHECK(toc[0].spine == 1);
}

TEST_CASE("every entry names a spine index that exists") {
  // The invariant the Contents screen rests on: selecting any row must be openable.
  for (const auto& fx : {std::pair{epubfix::kEpubToc, epubfix::kEpubTocLen},
                         std::pair{epubfix::kEpubTocRepeats, epubfix::kEpubTocRepeatsLen},
                         std::pair{epubfix::kEpubTocNested, epubfix::kEpubTocNestedLen},
                         std::pair{epubfix::kEpubTocOffSpine, epubfix::kEpubTocOffSpineLen}}) {
    FakeFileSystem fs = cardWith(asBytes(fx.first, fx.second));
    bool ok = false;
    const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
    REQUIRE(ok);
    for (const reader::TocEntry& e : toc) {
      CHECK(e.spine >= 0);
      CHECK(e.spine < 2);  // both fixtures have a two-entry spine
      CHECK_FALSE(e.label.empty());
    }
  }
}

TEST_CASE("a broken archive is a refusal with a reason, not an empty list") {
  // Distinct from "no table of contents": one means the book cannot be read at all.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubNoOpf, epubfix::kEpubNoOpfLen));
  bool ok = true;
  const char* why = "";
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok, &why);
  CHECK_FALSE(ok);
  CHECK(toc.empty());
  CHECK_FALSE(std::string(why).empty());
}

TEST_CASE("a missing book file is refused") {
  FakeFileSystem fs;
  std::vector<reader::TocEntry> toc;
  const char* why = "";
  CHECK_FALSE(reader::loadToc(fs, "/books/nope.epub", toc, &why));
  CHECK_FALSE(std::string(why).empty());
}

TEST_CASE("the entry for a spine index is the FIRST one naming it") {
  // IT WAS THE LAST FOR TWO PHASES, on the argument that "the later ones are further
  // into the file, so the last is the closest thing to where you are". The premise is
  // true and the conclusion needs the reader to be at the END of the file, which is
  // not where they are: a fragment is STRIPPED before the match, so every entry in a
  // group resolves to that file's start and nothing here knows any offsets. The first
  // entry is the only one that can be proved not to be AHEAD of the reader, and naming
  // a landmark they have not reached is the error that misleads -- reading_position.h
  // grades the same trade the same way ("the top of the right paragraph beats the
  // front of the book, which beats nothing").
  //
  // What the Reader's header band and Contents' NOW marker both need, and they must
  // agree: two screens naming the reader's chapter differently is two spellings of one
  // fact.
  const std::vector<reader::TocEntry> toc = {
      {0, 1, "Cover"}, {1, 1, "Part one"}, {1, 2, "Part two"}};
  CHECK(reader::tocIndexForSpine(toc, 0) == 0);
  CHECK(reader::tocIndexForSpine(toc, 1) == 1);
  CHECK(reader::tocIndexForSpine(toc, 9) == -1);
  CHECK(reader::tocIndexForSpine({}, 0) == -1);
}

TEST_CASE("nesting is recorded as a depth, because the board groups by it") {
  // Corrects a wrong measurement: one of the four books measured is THREE levels deep
  // -- ten section headers over eighty-four chapters -- and Contents.dc.html draws
  // exactly that grouping. Flattening would have shown a section header as a peer of
  // the chapters inside it.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubTocNested, epubfix::kEpubTocNestedLen));
  bool ok = false;
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
  REQUIRE(ok);
  REQUIRE(toc.size() == 4);
  // Outer, inner, outer, inner -- the order a document walk produces, and the order a
  // list draws.
  CHECK(toc[0].depth == 1);
  CHECK(toc[1].depth == 2);
  CHECK(toc[2].depth == 1);
  CHECK(toc[3].depth == 2);
  CHECK(toc[1].label.find("(inner)") != std::string::npos);
  // A child records ITS level, not its parent's -- committed before the depth drops.
  CHECK(toc[1].spine == toc[0].spine);
}

TEST_CASE("a flat NCX is all depth 1") {
  // Two of the four measured books are flat, so this is the common shape and not an
  // edge case; a screen that indented everything would be wrong for half the library.
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubToc, epubfix::kEpubTocLen));
  bool ok = false;
  const std::vector<reader::TocEntry> toc = tocOf(fs, &ok);
  REQUIRE(ok);
  REQUIRE_FALSE(toc.empty());
  for (const reader::TocEntry& e : toc) CHECK(e.depth == 1);
}
