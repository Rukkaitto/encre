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

TEST_CASE("a spine entry the book's contents skip still gets a row") {
  // READABLE BY PAGING, UNREACHABLE BY JUMPING -- the defect this closes. Reported
  // off Digital Minimalism, whose Conclusion is spine 15: the publisher styled its
  // title as `<p class="x03-Chapter-Title-BRK">` where every real chapter uses an
  // `<h2>`, their generator walks headings, and the chapter lost its navPoint. The
  // NCX runs `... spine 14, spine 16 ...` and nothing names 15.
  //
  // THE NAME IS NOT RECOVERABLE AND THAT WAS MEASURED, not assumed, over the 133
  // mid-book gaps in ~/.cache/encre-corpus: the EPUB 3 nav document names 0 of them
  // (both tables come from one generator, so they skip the same entries), the
  // chapter's own `<h1>`-`<h6>` names 35 (26.3%), and `<title>` is present for 96%
  // and useless -- this book's reads `Continued, Digital Minimalism`. So the row
  // carries the POSITION, which is what the reader's header band has always fallen
  // back to for exactly this case.
  std::vector<reader::TocEntry> toc = {
      {13, 2, "6: Reclaim Leisure"}, {14, 2, "7: Join the Attention Resistance"},
      {16, 1, "Acknowledgments"},    {17, 1, "Notes"}};
  CHECK(reader::fillTocGaps(toc, 20) == 1);
  REQUIRE(toc.size() == 5);
  // IN SPINE ORDER, where the reader would page into it.
  CHECK(toc[2].spine == 15);
  CHECK(toc[2].label == "CH. 16");
  // AND IT IS THEN FINDABLE BY THE ONE FUNCTION BOTH SCREENS ASK. Before this it
  // answered -1, so Contents marked no row at all and the reader's header fell
  // through to its own copy of the same string.
  CHECK(reader::tocIndexForSpine(toc, 15) == 2);
}

TEST_CASE("a synthesised row can never become a section header") {
  // `ContentsScreen::isHeaderAt` is "the next entry sits deeper than this one", and a
  // header is drawn as a tracked-caps label the focus SKIPS -- so a synthesised row
  // that landed at a shallower depth than its successor would be unreachable, which
  // is the defect this function exists to close, reintroduced by its own fix.
  //
  // TAKING THE FOLLOWING ENTRY'S DEPTH MAKES THAT STRUCTURAL rather than checked:
  // `next.depth > synth.depth` is false when they are equal, whatever the book's
  // shape. The gap-between-named-entries rule guarantees a following entry exists.
  // THE FIXTURE HAS TO PUT THE TWO CANDIDATE RULES ON DIFFERENT ANSWERS, and the
  // first one written here did not -- it gave the gap neighbours at equal depths, so
  // "take the following entry's depth" and "take the preceding entry's" agreed and a
  // mutation swapping them passed all 1,359,370 assertions. A mutation tells you
  // about your INPUT before it tells you about your test.
  //
  // A part divider followed by its first chapter is the shape that separates them,
  // and it is the shape that bites: the gap's PRECEDING entry is shallower than its
  // FOLLOWING one, so taking the preceding depth makes `next.depth > synth.depth`
  // true -- the synthesised row becomes a section header, the focus skips it, and it
  // is unreachable. Which is this function's own defect, reintroduced by its fix.
  std::vector<reader::TocEntry> toc = {{0, 1, "Part I"}, {2, 2, "Chapter two"}};
  REQUIRE(reader::fillTocGaps(toc, 8) == 1);
  REQUIRE(toc.size() == 3);
  CHECK(toc[1].spine == 1);
  CHECK(toc[1].depth == 2);          // the FOLLOWING entry's
  CHECK(toc[1].depth != toc[0].depth);  // and not the preceding one's
  // `ContentsScreen::isHeaderAt` is exactly this comparison, so equality here is the
  // whole guarantee.
  CHECK_FALSE(toc[2].depth > toc[1].depth);
}

TEST_CASE("only the gaps BETWEEN what the book names are filled") {
  // THE FRONT AND BACK MATTER ARE NOT GAPS. Digital Minimalism's spine carries 15
  // footnote files and a `next-reads.xhtml` after the last named entry, and its
  // cover before the first -- 16 rows of noise against the one chapter that is
  // missing. Bounding the fill by what the book itself named is what separates
  // them, and it is free: no size test, no decode.
  //
  // A SIZE FLOOR WAS MEASURED AND REFUSED. Text length separates cleanly (junk tops
  // out at 1,976 chars, real chapters start at 4,510) and is not knowable without
  // decoding every gap at book-open time; the archive's uncompressed size is free
  // and does NOT separate -- junk reaches 5,210 bytes where a real chapter starts at
  // 6,187. The cost of having none is a couple of front-matter rows on a minority of
  // books; see the corpus figures in toc.h.
  std::vector<reader::TocEntry> toc = {{2, 1, "Chapter one"}, {4, 1, "Chapter two"}};
  CHECK(reader::fillTocGaps(toc, 40) == 1);
  REQUIRE(toc.size() == 3);
  CHECK(toc[1].spine == 3);
  // Spines 0, 1 and 5..39 are outside the named range and stay unlisted.
  CHECK(toc.front().spine == 2);
  CHECK(toc.back().spine == 4);
}

TEST_CASE("a book whose contents have no gaps is left byte-identical") {
  // The common case -- 177 of the corpus's 207 books with a usable NCX -- and it
  // must cost nothing, because this runs on every book open.
  const std::vector<reader::TocEntry> before = {
      {0, 1, "Cover"}, {1, 1, "Part one"}, {1, 2, "Part two"}, {2, 1, "End"}};
  std::vector<reader::TocEntry> toc = before;
  CHECK(reader::fillTocGaps(toc, 3) == 0);
  REQUIRE(toc.size() == before.size());
  for (size_t i = 0; i < before.size(); ++i) {
    CHECK(toc[i].spine == before[i].spine);
    CHECK(toc[i].depth == before[i].depth);
    CHECK(toc[i].label == before[i].label);
  }
  // A GROUP IS NOT A GAP. Several navPoints naming one spine entry is the majority
  // case (109 of 206 corpus books), and the spine indices they skip between them are
  // not holes -- `Part one` and `Part two` above both name spine 1.
}

TEST_CASE("an empty contents is not filled in, and neither is one past the cap") {
  // A BOOK WITH NO NCX GETS NO SYNTHETIC CONTENTS. There is no named range to bound
  // the fill by, so the only rule available would be "every spine entry", which is a
  // different feature with a different argument -- and `Contents` already has an
  // answer for a book that cannot name its chapters.
  std::vector<reader::TocEntry> empty;
  CHECK(reader::fillTocGaps(empty, 40) == 0);
  CHECK(empty.empty());

  // AND THE CAP IS THE ONE THE LIST ALREADY HAS. `kMaxTocEntries` sizes a vector and
  // a file is free to claim anything; a gap fill must not be the way past it.
  std::vector<reader::TocEntry> wide = {{0, 1, "first"},
                                        {static_cast<int>(reader::kMaxTocEntries) * 4, 1, "last"}};
  reader::fillTocGaps(wide, static_cast<int>(reader::kMaxTocEntries) * 8);
  CHECK(wide.size() <= reader::kMaxTocEntries);
}
