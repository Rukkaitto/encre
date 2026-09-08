// Progress on the card: the sidecars, the pointer, and the write that must not be
// fatal when it fails.
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/json.h"
#include "reader/book.h"
#include "reader/reading_store.h"

using reader::LastRead;
using reader::ReadingPosition;
using reader::SaveResult;

namespace {

ReadingPosition pos(std::string path = "/books/a.epub", int spine = 3, int block = 9) {
  ReadingPosition p;
  p.bookPath = std::move(path);
  p.spine = spine;
  p.block = block;
  p.line = 2;
  p.bookBytes = 500000;
  p.ppem = 32;
  p.columnW = 492;
  return p;
}

LastRead last() {
  LastRead l;
  l.bookPath = "/books/a.epub";
  l.title = "Le Fl\xC3\xA9""au";
  l.author = "Stephen King";
  l.percent = 42;
  l.spine = 7;
  l.chapter = "PREMI\xC3\x88RE PARTIE";
  return l;
}

// A book whose chapters have the byte shape progressPercent reads. No archive
// behind it -- this function only ever looks at the spans.
reader::OpenedBook bookOf(std::initializer_list<uint32_t> sizes) {
  reader::OpenedBook b;
  b.path = "/books/a.epub";
  for (uint32_t s : sizes) {
    reader::ChapterSpan c;
    c.uncompressedSize = s;
    c.compressedSize = s / 3 + 1;
    b.chapters.push_back(c);
  }
  return b;
}

}  // namespace

// --- The sidecars ------------------------------------------------------------

TEST_CASE("a saved position comes back") {
  FakeFileSystem fs;
  const ReadingPosition p = pos();
  REQUIRE(reader::savePosition(fs, p) == SaveResult::Written);
  ReadingPosition got;
  REQUIRE(reader::loadPosition(fs, p.bookPath, got));
  CHECK(got == p);
}

TEST_CASE("saving the same position twice writes once") {
  // The point of a byte-identical dump: a save fires when the reader leaves a book,
  // and leaving a book they did not move in must not cost a card write.
  FakeFileSystem fs;
  const ReadingPosition p = pos();
  CHECK(reader::savePosition(fs, p) == SaveResult::Written);
  CHECK(reader::savePosition(fs, p) == SaveResult::Unchanged);
  CHECK(reader::savePosition(fs, p) == SaveResult::Unchanged);
  // ...and a real change writes again.
  ReadingPosition moved = p;
  moved.line += 1;
  CHECK(reader::savePosition(fs, moved) == SaveResult::Written);
}

TEST_CASE("no position for a book that has none") {
  FakeFileSystem fs;
  ReadingPosition got;
  CHECK_FALSE(reader::loadPosition(fs, "/books/never-opened.epub", got));
}

TEST_CASE("two books keep two positions") {
  FakeFileSystem fs;
  const ReadingPosition a = pos("/books/a.epub", 3);
  const ReadingPosition b = pos("/books/b.epub", 40);
  REQUIRE(reader::savePosition(fs, a) == SaveResult::Written);
  REQUIRE(reader::savePosition(fs, b) == SaveResult::Written);
  ReadingPosition got;
  REQUIRE(reader::loadPosition(fs, "/books/a.epub", got));
  CHECK(got.spine == 3);
  REQUIRE(reader::loadPosition(fs, "/books/b.epub", got));
  CHECK(got.spine == 40);
}

TEST_CASE("a record found under another book's name is refused") {
  // The filename is a hash, so this is reachable. Simulated by writing a's record
  // into b's slot, which is exactly what a collision looks like from here.
  FakeFileSystem fs;
  const ReadingPosition a = pos("/books/a.epub");
  REQUIRE(fs.writeAll(reader::statePathFor("/books/b.epub"), reader::serialise(a)));
  ReadingPosition got;
  CHECK_FALSE(reader::loadPosition(fs, "/books/b.epub", got));
  // ...and the grader refuses it independently, which is the second of the two
  // guards this hazard has.
  CHECK(reader::fitOf(a, "/books/b.epub", a.bookBytes, a.ppem, a.columnW) ==
        reader::PositionFit::Unusable);
}

TEST_CASE("a position with no book path is refused rather than written") {
  // It could never match a book again, so it would be an unreachable file squatting
  // on a name a real book might hash to.
  FakeFileSystem fs;
  ReadingPosition p = pos();
  p.bookPath.clear();
  CHECK(reader::savePosition(fs, p) == SaveResult::Failed);
}

TEST_CASE("a corrupt sidecar reads as no position, not as a wrong one") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(reader::statePathFor("/books/a.epub"), "{ this is not json"));
  ReadingPosition got;
  CHECK_FALSE(reader::loadPosition(fs, "/books/a.epub", got));
  // And a truncated one -- a save interrupted by power loss.
  const std::string whole = reader::serialise(pos());
  REQUIRE(fs.writeAll(reader::statePathFor("/books/a.epub"), whole.substr(0, whole.size() / 2)));
  CHECK_FALSE(reader::loadPosition(fs, "/books/a.epub", got));
}

// --- The write-protected card, which is the hazard ---------------------------

TEST_CASE("A FAILED SAVE IS REPORTED, NOT THROWN") {
  // The case this matters for: a card that reads fine and refuses writes, which a
  // physical write-protect tab produces. On the device a failed writeAll calls
  // noteCardGone(), so a caller that treated Failed as fatal would eject the reader
  // from a book they can still read. What this pins is that the store reports it and
  // changes nothing.
  FakeFileSystem fs;
  fs.setFailWrites(true);
  const ReadingPosition p = pos();
  CHECK(reader::savePosition(fs, p) == SaveResult::Failed);
  CHECK(reader::saveLastRead(fs, last()) == SaveResult::Failed);
  // Nothing landed, and nothing half-landed.
  ReadingPosition got;
  CHECK_FALSE(reader::loadPosition(fs, p.bookPath, got));
  LastRead l;
  CHECK_FALSE(reader::loadLastRead(fs, l));
}

TEST_CASE("reading still works on a card that refuses writes") {
  // The whole reason Failed must not be fatal: everything else about this card is
  // fine, including a position saved before the tab was set.
  FakeFileSystem fs;
  const ReadingPosition p = pos();
  REQUIRE(reader::savePosition(fs, p) == SaveResult::Written);
  fs.setFailWrites(true);
  ReadingPosition got;
  REQUIRE(reader::loadPosition(fs, p.bookPath, got));
  CHECK(got == p);
}

TEST_CASE("an unchanged save does not fail on a write-protected card") {
  // It writes nothing, so there is nothing to refuse. This is what keeps a reader who
  // is not moving from generating a failure per save on a locked card.
  FakeFileSystem fs;
  const ReadingPosition p = pos();
  REQUIRE(reader::savePosition(fs, p) == SaveResult::Written);
  fs.setFailWrites(true);
  CHECK(reader::savePosition(fs, p) == SaveResult::Unchanged);
}

// --- The pointer -------------------------------------------------------------

TEST_CASE("the last-read pointer round-trips, accents included") {
  FakeFileSystem fs;
  const LastRead l = last();
  REQUIRE(reader::saveLastRead(fs, l) == SaveResult::Written);
  LastRead got;
  REQUIRE(reader::loadLastRead(fs, got));
  CHECK(got.bookPath == l.bookPath);
  CHECK(got.title == l.title);
  CHECK(got.author == l.author);
  CHECK(got.percent == l.percent);
  CHECK(got.spine == l.spine);
  CHECK(got.chapter == l.chapter);
}

TEST_CASE("a pointer written before the chapter key still loads, with no chapter") {
  // THE STATED PRICE OF NOT BUMPING kLastReadVersion. Every card in the world holds a
  // pointer without this key, and the alternative to reading it as absent is refusing
  // the record -- which costs the reader their whole CONTINUE block, title and author
  // and percentage together, to gain a chapter name one save earlier than it arrives
  // anyway. So the key is OPTIONAL and empty is a legal answer; Home draws that line
  // blank rather than falling back to the spine position it was reported for.
  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/.reader/last.json",
                      "{\"author\":\"Stephen King\",\"path\":\"/books/a.epub\",\"percent\":42,"
                      "\"spine\":7,\"spineCount\":92,\"title\":\"Le Fleau\",\"version\":1}"));
  LastRead got;
  REQUIRE(reader::loadLastRead(fs, got));
  // Everything the old pointer DID carry survives -- this is a degradation, not a
  // rejection -- including the `spineCount` key nothing reads any more.
  CHECK(got.bookPath == "/books/a.epub");
  CHECK(got.title == "Le Fleau");
  CHECK(got.percent == 42);
  CHECK(got.spine == 7);
  CHECK(got.chapter.empty());
}

TEST_CASE("a book with no contents stores the Reader's position fallback, not nothing") {
  // updateChapterLabel falls back to `CH. 08` for a book with no NCX, so the pointer
  // carries that string and Home shows it -- a position with NO TOTAL, which is what
  // the Reader's own footer says and the only handle such a book offers. The empty
  // case above is therefore an OLD POINTER and never a book without contents, and the
  // two must not be conflated: one is a blank line, the other is a real answer.
  FakeFileSystem fs;
  LastRead l = last();
  l.chapter = "CH. 08";
  REQUIRE(reader::saveLastRead(fs, l) == SaveResult::Written);
  LastRead got;
  REQUIRE(reader::loadLastRead(fs, got));
  CHECK(got.chapter == "CH. 08");
}

TEST_CASE("a book with no author still gives a usable pointer") {
  // An EPUB is not obliged to carry one. Home draws the line blank rather than
  // refusing to name the book.
  FakeFileSystem fs;
  LastRead l = last();
  l.author.clear();
  l.title.clear();
  REQUIRE(reader::saveLastRead(fs, l) == SaveResult::Written);
  LastRead got;
  REQUIRE(reader::loadLastRead(fs, got));
  CHECK(got.bookPath == l.bookPath);
  CHECK(got.author.empty());
}

TEST_CASE("an out-of-range percentage is clamped both ways") {
  FakeFileSystem fs;
  LastRead l = last();
  l.percent = 4000;
  REQUIRE(reader::saveLastRead(fs, l) == SaveResult::Written);
  LastRead got;
  REQUIRE(reader::loadLastRead(fs, got));
  CHECK(got.percent == 100);
  l.percent = -5;
  REQUIRE(reader::saveLastRead(fs, l) == SaveResult::Written);
  REQUIRE(reader::loadLastRead(fs, got));
  CHECK(got.percent == 0);
}

TEST_CASE("forgetting the pointer leaves the per-book positions alone") {
  // Spec 4.0: deleting a book "never erases reading progress". So a book that comes
  // back still knows where the reader was, even though Home has stopped naming it.
  FakeFileSystem fs;
  const ReadingPosition p = pos();
  REQUIRE(reader::savePosition(fs, p) == SaveResult::Written);
  REQUIRE(reader::saveLastRead(fs, last()) == SaveResult::Written);
  CHECK(reader::forgetLastRead(fs));
  LastRead l;
  CHECK_FALSE(reader::loadLastRead(fs, l));
  ReadingPosition got;
  REQUIRE(reader::loadPosition(fs, p.bookPath, got));
  CHECK(got == p);
}

TEST_CASE("forgetting a pointer that is not there succeeds") {
  // The caller asked for a state, not for an event.
  FakeFileSystem fs;
  CHECK(reader::forgetLastRead(fs));
}

TEST_CASE("a pointer from another version reads as none") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(reader::kLastReadPath,
                      "{\n  \"version\": 99,\n  \"path\": \"/books/a.epub\"\n}\n"));
  LastRead got;
  CHECK_FALSE(reader::loadLastRead(fs, got));
}

// --- Progress by bytes -------------------------------------------------------

TEST_CASE("progress is a fraction of the book's BYTES") {
  // Four equal chapters, so the arithmetic is checkable by hand.
  const reader::OpenedBook b = bookOf({1000, 1000, 1000, 1000});
  CHECK(reader::progressPercent(b, 0, 1, 0) == 0);    // the very start
  CHECK(reader::progressPercent(b, 1, 1, 0) == 25);   // one chapter behind
  CHECK(reader::progressPercent(b, 2, 1, 0) == 50);
  CHECK(reader::progressPercent(b, 3, 1, 0) == 75);
}

TEST_CASE("progress interpolates within the open chapter when its pages are known") {
  const reader::OpenedBook b = bookOf({1000, 1000, 1000, 1000});
  // Four pages into a nine-page chapter 2 of 4: 1000 bytes behind, plus 1000*4/9 of
  // this one, over 4000 -- 36.1%, rounded to 36. (Written as 38 first, with the
  // arithmetic left as a question in this comment; the code was right.)
  CHECK(reader::progressPercent(b, 1, 5, 9) == 36);
  // Page 1 of a chapter is none of it read, which is what makes arriving at a
  // chapter not jump the percentage.
  CHECK(reader::progressPercent(b, 1, 1, 9) == 25);
  // The last page of the last chapter is not 100% -- there is still a page to read.
  CHECK(reader::progressPercent(b, 3, 9, 9) == 97);
}

TEST_CASE("unequal chapters weight by size, which is the point of using bytes") {
  // A book that is mostly its last chapter: being at the start of that chapter is
  // early in the book, not 50% through it.
  const reader::OpenedBook b = bookOf({100, 9900});
  CHECK(reader::progressPercent(b, 1, 1, 0) == 1);
}

TEST_CASE("progress refuses to invent a number it cannot have") {
  CHECK(reader::progressPercent(reader::OpenedBook{}, 0, 1, 0) == 0);   // no chapters
  CHECK(reader::progressPercent(bookOf({0, 0}), 1, 1, 0) == 0);          // no bytes
  CHECK(reader::progressPercent(bookOf({100, 100}), -1, 1, 0) == 0);     // no spine
}

TEST_CASE("progress is bounded even when the inputs are not") {
  const reader::OpenedBook b = bookOf({1000, 1000});
  // A spine past the end (a book replaced with a shorter one) and a page past the
  // chapter's count both have to stay inside 0..100.
  CHECK(reader::progressPercent(b, 99, 1, 0) == 100);
  const int p = reader::progressPercent(b, 1, 999, 5);
  CHECK(p >= 0);
  CHECK(p <= 100);
}

// --- The progress index -------------------------------------------------------

TEST_CASE("the progress index reads every started book in one pass") {
  FakeFileSystem fs;
  ReadingPosition a = pos("/books/a.epub", 3);
  a.percent = 31;
  ReadingPosition b = pos("/books/Classics/b.epub", 12);
  b.percent = 6;
  REQUIRE(reader::savePosition(fs, a) == SaveResult::Written);
  REQUIRE(reader::savePosition(fs, b) == SaveResult::Written);

  std::vector<reader::ProgressEntry> index;
  REQUIRE(reader::loadProgressIndex(fs, index));
  CHECK(index.size() == 2);
  CHECK(reader::percentFor(index, "/books/a.epub") == 31);
  CHECK(reader::percentFor(index, "/books/Classics/b.epub") == 6);
  // NOT STARTED is -1, not 0: a book at 0% has been opened and a book that has not
  // been opened reads NEW, and the Library draws those differently.
  CHECK(reader::percentFor(index, "/books/never.epub") == -1);
}

TEST_CASE("a card nothing has been read on has an empty index, not a failure") {
  // The normal state of every card until the first book is opened -- so an absent
  // /.reader/state must not read as an error the Library has to handle.
  FakeFileSystem fs;
  std::vector<reader::ProgressEntry> index;
  CHECK(reader::loadProgressIndex(fs, index));
  CHECK(index.empty());
}

TEST_CASE("one corrupt record costs one book its percentage and no more") {
  FakeFileSystem fs;
  ReadingPosition good = pos("/books/good.epub");
  good.percent = 77;
  REQUIRE(reader::savePosition(fs, good) == SaveResult::Written);
  // A save cut by a power loss, sitting in the same directory.
  REQUIRE(fs.writeAll(reader::statePathFor("/books/torn.epub"), "{\n  \"path\": \"/bo"));

  std::vector<reader::ProgressEntry> index;
  REQUIRE(reader::loadProgressIndex(fs, index));
  CHECK(reader::percentFor(index, "/books/good.epub") == 77);
  CHECK(reader::percentFor(index, "/books/torn.epub") == -1);
}

TEST_CASE("the percentage survives the sidecar round trip") {
  // It is stored rather than derived precisely so the Library need not open a book to
  // learn it, so it had better come back.
  ReadingPosition p = pos();
  p.percent = 64;
  ReadingPosition out;
  REQUIRE(reader::parsePosition(reader::serialise(p), out));
  CHECK(out.percent == 64);
}

TEST_CASE("a percentage outside 0..100 is clamped on the way in") {
  // It is read straight onto a screen, and the file is hand-editable.
  reader::JsonObject o;
  o.setInt("version", reader::kPositionVersion);
  o.setString("path", "/books/a.epub");
  o.setInt("spine", 1);
  o.setInt("percent", 900);
  ReadingPosition out;
  REQUIRE(reader::parsePosition(o.dump(), out));
  CHECK(out.percent == 100);
  o.setInt("percent", -3);
  REQUIRE(reader::parsePosition(o.dump(), out));
  CHECK(out.percent == 0);
}

// --- Book details' rows come from the sidecar -----------------------------------

TEST_CASE("the sidecar carries the chapter name, for Current story") {
  // Stored for the same reason `percent` is, and a stronger case: recovering it means
  // opening the book's archive AND parsing its NCX, where the Library needs the answer
  // for a row it draws without opening anything.
  FakeFileSystem fs;
  ReadingPosition p = pos();
  p.percent = 31;
  p.chapter = "LIVRE I";
  REQUIRE(reader::savePosition(fs, p) == SaveResult::Written);

  std::vector<reader::ProgressEntry> index;
  REQUIRE(reader::loadProgressIndex(fs, index));
  const reader::ProgressEntry* e = reader::progressFor(index, p.bookPath);
  REQUIRE(e != nullptr);
  CHECK(e->percent == 31);
  CHECK(e->chapter == "LIVRE I");
  CHECK(reader::progressFor(index, "/books/never.epub") == nullptr);
}

TEST_CASE("a sidecar written before the chapter field existed still loads") {
  // Every position saved by an earlier firmware lacks it, and a position is perfectly
  // usable without a chapter name -- the row simply draws blank. Refusing the record
  // would lose the reader their place to gain a label.
  reader::JsonObject o;
  o.setInt("version", reader::kPositionVersion);
  o.setString("path", "/books/a.epub");
  o.setInt("spine", 4);
  o.setInt("percent", 12);
  ReadingPosition out;
  REQUIRE(reader::parsePosition(o.dump(), out));
  CHECK(out.spine == 4);
  CHECK(out.percent == 12);
  CHECK(out.chapter.empty());
}

TEST_CASE("an accented chapter name survives the sidecar") {
  // Real ones are accented, and the theme shouts some of them.
  ReadingPosition p = pos();
  p.chapter = "PREMI\xC3\x88RE PARTIE";
  ReadingPosition out;
  REQUIRE(reader::parsePosition(reader::serialise(p), out));
  CHECK(out.chapter == p.chapter);
}

TEST_CASE("loadProgressIndex reports whether a book was finished") {
  // The Library's row says DONE for a finished book, and the row is drawn from this
  // index -- so the flag has to travel the same road `percent` and `chapter` do,
  // rather than being recovered by opening the book.
  FakeFileSystem fs;
  reader::ReadingPosition p;
  p.bookPath = "/books/Jane Eyre.epub";
  p.spine = 9;
  p.percent = 100;
  p.finished = true;
  REQUIRE(reader::savePosition(fs, p) == reader::SaveResult::Written);

  std::vector<reader::ProgressEntry> index;
  REQUIRE(reader::loadProgressIndex(fs, index));
  const reader::ProgressEntry* e = reader::progressFor(index, "/books/Jane Eyre.epub");
  REQUIRE(e != nullptr);
  CHECK(e->finished == true);
}

TEST_CASE("a book that was merely read to the end is not finished") {
  // 100% and "the reader said they were done" are different claims, and the row has
  // one slot -- so the default must not be inferred from the percentage.
  FakeFileSystem fs;
  ReadingPosition p = pos("/books/nearly.epub");
  p.percent = 100;
  REQUIRE(reader::savePosition(fs, p) == SaveResult::Written);

  std::vector<reader::ProgressEntry> index;
  REQUIRE(reader::loadProgressIndex(fs, index));
  const reader::ProgressEntry* e = reader::progressFor(index, "/books/nearly.epub");
  REQUIRE(e != nullptr);
  CHECK(e->finished == false);
}
