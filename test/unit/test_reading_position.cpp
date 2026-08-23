#include <string>

#include "doctest.h"
#include "reader/json.h"
#include "reader/reading_position.h"

using reader::PositionFit;
using reader::ReadingPosition;

namespace {

ReadingPosition sample() {
  ReadingPosition p;
  p.bookPath = "/books/Fleau.epub";
  p.spine = 7;
  p.block = 42;
  p.line = 3;
  p.bookBytes = 12'700'000;
  p.ppem = 32;
  p.columnW = 492;
  return p;
}

}  // namespace

TEST_CASE("a position round-trips through its wire format") {
  const ReadingPosition in = sample();
  ReadingPosition out;
  REQUIRE(reader::parsePosition(reader::serialise(in), out));
  CHECK(out == in);
}

TEST_CASE("serialising an unchanged position is byte-identical") {
  // What lets the shell skip a write that would change nothing, and a card write is
  // the expensive thing here. It rests on dump() sorting its keys.
  CHECK(reader::serialise(sample()) == reader::serialise(sample()));
}

TEST_CASE("a non-ASCII book path survives the round trip") {
  // Real cards carry these. json.h stores raw UTF-8 and does not emit \uXXXX, so
  // the bytes must come back exactly -- a mangled path reads as a hash collision
  // and silently costs the book its position.
  ReadingPosition p = sample();
  p.bookPath = "/books/King, Stephen/Fl\xC3\xA9""au, Le/Fl\xC3\xA9""au, Le - Stephen King.epub";
  ReadingPosition out;
  REQUIRE(reader::parsePosition(reader::serialise(p), out));
  CHECK(out.bookPath == p.bookPath);
}

TEST_CASE("a path with a quote and a backslash survives, because a filename may hold them") {
  ReadingPosition p = sample();
  p.bookPath = "/books/He said \"go\"\\dir/b.epub";
  ReadingPosition out;
  REQUIRE(reader::parsePosition(reader::serialise(p), out));
  CHECK(out.bookPath == p.bookPath);
}

// --- Refusals ----------------------------------------------------------------

TEST_CASE("a truncated file is refused, which is what a lost power mid-save leaves") {
  const std::string whole = reader::serialise(sample());
  // UP TO THE CLOSING BRACE, not to the end of the string. dump() ends with a
  // newline, so the prefix one byte short of the whole thing is the complete object
  // with its trailing whitespace removed -- and that MUST still parse. Looping to
  // whole.size() asserted the opposite and failed here, which is the test being
  // wrong about what a truncation is rather than the parser being lenient.
  const size_t close = whole.rfind('}');
  REQUIRE(close != std::string::npos);
  // Every prefix that actually cuts into the record, not just one: a save can be
  // interrupted anywhere, and a prefix that happened to parse would be a position
  // assembled from half a file.
  for (size_t n = 0; n <= close; ++n) {
    ReadingPosition scratch;
    CHECK_FALSE(reader::parsePosition(whole.substr(0, n), scratch));
  }
  // The record itself parses with and without the trailing newline.
  ReadingPosition out;
  CHECK(reader::parsePosition(whole, out));
  CHECK(reader::parsePosition(whole.substr(0, close + 1), out));
}

TEST_CASE("a record from another version is refused, not read leniently") {
  reader::JsonObject o;
  o.setInt("version", reader::kPositionVersion + 1);
  o.setString("path", "/books/a.epub");
  o.setInt("spine", 3);
  ReadingPosition out;
  CHECK_FALSE(reader::parsePosition(o.dump(), out));
  // And a record with NO version at all -- a hand-written file, or a future format
  // that dropped the key.
  reader::JsonObject bare;
  bare.setString("path", "/books/a.epub");
  bare.setInt("spine", 3);
  CHECK_FALSE(reader::parsePosition(bare.dump(), out));
}

TEST_CASE("a record with no path is refused, because a collision could not be told from a match") {
  reader::JsonObject o;
  o.setInt("version", reader::kPositionVersion);
  o.setInt("spine", 3);
  ReadingPosition out;
  CHECK_FALSE(reader::parsePosition(o.dump(), out));
  o.setString("path", "");
  CHECK_FALSE(reader::parsePosition(o.dump(), out));
}

TEST_CASE("a spine-only record IS accepted, because a Rebound restore writes one") {
  // The optional fields are optional on purpose: refusing them would refuse a file
  // this code itself produces after a book was replaced on the card.
  reader::JsonObject o;
  o.setInt("version", reader::kPositionVersion);
  o.setString("path", "/books/a.epub");
  o.setInt("spine", 5);
  ReadingPosition out;
  REQUIRE(reader::parsePosition(o.dump(), out));
  CHECK(out.spine == 5);
  CHECK(out.block == 0);
  CHECK(out.line == 0);
  CHECK(out.ppem == 0);
}

TEST_CASE("negative indices are clamped, not trusted") {
  reader::JsonObject o;
  o.setInt("version", reader::kPositionVersion);
  o.setString("path", "/books/a.epub");
  o.setInt("spine", -4);
  o.setInt("block", -1);
  o.setInt("line", -9);
  ReadingPosition out;
  REQUIRE(reader::parsePosition(o.dump(), out));
  CHECK(out.spine == 0);
  CHECK(out.block == 0);
  CHECK(out.line == 0);
}

TEST_CASE("garbage of every shape is refused rather than partially read") {
  ReadingPosition out;
  for (const char* bad : {"", "{", "}", "[]", "null", "{\"version\"", "not json at all",
                          "{\"version\": 1, \"path\": \"/a\", \"spine\": 1.5}",
                          "{\"version\": 1, \"path\": \"/a\", \"spine\": 1e3}"}) {
    CHECK_FALSE(reader::parsePosition(bad, out));
  }
}

// --- The staleness grading ---------------------------------------------------

TEST_CASE("an untouched book and an unchanged layout is an Exact fit") {
  const ReadingPosition p = sample();
  CHECK(reader::fitOf(p, p.bookPath, p.bookBytes, p.ppem, p.columnW) == PositionFit::Exact);
  const reader::PositionRestore r = reader::restoreFrom(p, PositionFit::Exact);
  CHECK(r.any);
  CHECK(r.spine == 7);
  CHECK(r.cursor == reader::Cursor{42, 3});
}

TEST_CASE("a different type size keeps the block and drops the line") {
  const ReadingPosition p = sample();
  // A line is a line WITHIN a block at one ppem; blocks are the document's.
  CHECK(reader::fitOf(p, p.bookPath, p.bookBytes, 41, p.columnW) == PositionFit::Relaid);
  CHECK(reader::fitOf(p, p.bookPath, p.bookBytes, p.ppem, 444) == PositionFit::Relaid);
  const reader::PositionRestore r = reader::restoreFrom(p, PositionFit::Relaid);
  CHECK(r.any);
  CHECK(r.spine == 7);
  CHECK(r.cursor == reader::Cursor{42, 0});  // the paragraph, not the line in it
}

TEST_CASE("a book whose bytes changed keeps only the spine") {
  const ReadingPosition p = sample();
  CHECK(reader::fitOf(p, p.bookPath, p.bookBytes + 1, p.ppem, p.columnW) == PositionFit::Rebound);
  const reader::PositionRestore r = reader::restoreFrom(p, PositionFit::Rebound);
  CHECK(r.any);
  CHECK(r.spine == 7);
  CHECK(r.cursor == reader::Cursor{});  // the top of the chapter
}

TEST_CASE("a changed book outranks a changed layout, because it is the weaker fit") {
  // Both differ. The answer must be the one that trusts LESS -- grading that took
  // the first mismatch it found would keep a block index inside a rewritten chapter.
  const ReadingPosition p = sample();
  CHECK(reader::fitOf(p, p.bookPath, p.bookBytes + 1, 41, 444) == PositionFit::Rebound);
}

TEST_CASE("a different path is Unusable whatever else agrees") {
  // The filename is a hash, so this is the collision check -- and every other field
  // agreeing is exactly what a collision looks like.
  const ReadingPosition p = sample();
  CHECK(reader::fitOf(p, "/books/other.epub", p.bookBytes, p.ppem, p.columnW) ==
        PositionFit::Unusable);
  const reader::PositionRestore r = reader::restoreFrom(p, PositionFit::Unusable);
  CHECK_FALSE(r.any);
  CHECK(r.spine == 0);
}

TEST_CASE("a record with no geometry cannot claim a line at this one") {
  // What a spine-only file parses to. Claiming Exact for it would restore line 0 of
  // block 0 as though it had been measured, which is a coincidence rather than a
  // position.
  ReadingPosition p;
  p.bookPath = "/books/a.epub";
  p.spine = 2;
  CHECK(reader::fitOf(p, p.bookPath, 0, 32, 492) == PositionFit::Rebound);
}

// --- The sidecar path --------------------------------------------------------

TEST_CASE("a state path is stable, bounded and inside /.reader/state") {
  const std::string a = reader::statePathFor("/books/Fleau.epub");
  CHECK(a == reader::statePathFor("/books/Fleau.epub"));  // stable
  CHECK(a.rfind("/.reader/state/", 0) == 0);
  CHECK(a.size() == std::string("/.reader/state/").size() + 8 + 5);  // 8 hex + ".json"
}

TEST_CASE("a state path holds no character a filesystem would refuse") {
  // The whole reason it is a hash: a book path contains '/' by construction, and FAT
  // forbids more than that. A long accented title must not reach the filename.
  const std::string p = reader::statePathFor(
      "/books/King, Stephen/Fl\xC3\xA9""au: \"Le\"?*|<>\\/a very long title indeed.epub");
  const std::string leaf = p.substr(std::string("/.reader/state/").size());
  REQUIRE(leaf.size() == 13);
  // The extension is fixed, and its letters are not hex -- checking the whole leaf
  // against [0-9a-f.] failed on `.json` itself, which was the test forgetting its
  // own suffix rather than the path being unsafe.
  CHECK(leaf.substr(8) == ".json");
  for (char c : leaf.substr(0, 8)) {
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    CHECK(hex);
  }
}

TEST_CASE("different books get different state paths") {
  // Not a claim that collisions are impossible -- they are handled by the stored
  // path -- but the everyday cases must not collide, or every book in a folder
  // would share one file.
  CHECK(reader::statePathFor("/books/a.epub") != reader::statePathFor("/books/b.epub"));
  CHECK(reader::statePathFor("/books/a.epub") != reader::statePathFor("/books/a.epub "));
  CHECK(reader::statePathFor("/books/x/a.epub") != reader::statePathFor("/books/y/a.epub"));
}
