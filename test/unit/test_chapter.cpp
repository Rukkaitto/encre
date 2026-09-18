// LETTING GO OF A CHAPTER'S STREAM AND TAKING IT BACK.
//
// ChapterReader's other behaviour -- begin, rewind, the block stream, the refusals --
// is exercised in test_book.cpp, which owns the openBook seam these locations come
// from. This file is only the release/reacquire pair, over the same kEpubGood fixture
// so the two files cannot disagree about what a chapter's blocks are.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/book.h"
#include "reader/chapter.h"

namespace {

// The fixtures are unsigned-char arrays; the filesystem takes bytes.
std::string_view asBytes(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

// A card with a real EPUB on it, opened to a location. `kEpubGood` carries both
// storage methods as an EPUB does -- a stored mimetype and DEFLATED content -- and
// the DEFLATED half is the one that matters here: see the fixture assertion below.
struct OpenChapter {
  FakeFileSystem fs;
  reader::OpenedBook book;

  OpenChapter() {
    REQUIRE(fs.writeAll("/books/book.epub",
                        asBytes(epubfix::kEpubGood, epubfix::kEpubGoodLen)));
    const char* why = "";
    REQUIRE_MESSAGE(reader::openBook(fs, "/books/book.epub", book, &why), std::string(why));
    REQUIRE(book.chapterCount() >= 1);
    // THE FIXTURE HAS TO BE DEFLATED OR HALF THIS FILE IS VACUOUS. bytesRead() reads
    // the inflate source's own count and inflateWindowHeld() reads the
    // decoder's own allocation -- a STORED entry has neither, so both would answer
    // "released" before the release and every assertion below would be 0 == 0.
    REQUIRE(book.locate(0).deflated);
  }
};

std::vector<std::string> drain(reader::ChapterReader& ch) {
  std::vector<std::string> out;
  reader::Block b;
  while (ch.next(b)) {
    out.push_back(b.text);
    b = reader::Block{};  // dropped, as the reader's caller drops it
  }
  return out;
}

}  // namespace

TEST_CASE("a released ChapterReader holds nothing and can be begun again") {
  // WHAT THE PEEK'S MEMORY DESIGN RESTS ON. A live chapter peaks at 69,884 bytes with
  // a 36,956-byte single allocation, against a measured 45,840-byte heap floor -- so
  // two of them do not fit, and the Reader beneath a peek has to let go of its stream
  // and get it back.
  //
  // ASSERTED RATHER THAN REASONED ABOUT, because "the pointers are reset" is exactly
  // the kind of claim that stays true in a comment long after it has stopped being
  // true in the code -- and in this case the obvious version of the claim was already
  // false: the inflate window is NOT behind one of this class's unique_ptrs.
  OpenChapter card;
  reader::ChapterReader ch;
  REQUIRE(ch.begin(card.fs, card.book.locate(0)));
  CHECK(ch.held());

  const std::vector<std::string> before = drain(ch);
  REQUIRE(before.size() >= 1);
  // The fixture reaches the two things the release has to give back, so a mutation to
  // either can bite. Without this the assertions after release() are 0 == 0.
  REQUIRE(ch.bytesRead() > 0);
  REQUIRE(ch.inflateWindowHeld());

  ch.release();
  CHECK_FALSE(ch.held());
  // THE ALLOCATION IS THE POINT, NOT THE POINTER. held() reads blocks_, so a release
  // that dropped blocks_ and kept the 36,956-byte inflate scratch would satisfy it
  // while freeing none of the memory the peek needs. These two see the rest: read
  // through the InflateSource wrapper, and the decoder's own block underneath it.
  CHECK(ch.bytesRead() == 0);
  CHECK_FALSE(ch.inflateWindowHeld());

  // AND IT COMES BACK. Not "it can be begun" in the abstract: the same blocks, in the
  // same order, because a release that quietly lost the location would produce a
  // reader that opens and yields nothing -- which is indistinguishable from a chapter
  // that ended.
  REQUIRE(ch.begin(card.fs, card.book.locate(0)));
  CHECK(ch.held());
  CHECK(ch.inflateWindowHeld());
  CHECK(drain(ch) == before);
}

TEST_CASE("releasing twice is not an error, and a released reader yields nothing") {
  // IDEMPOTENT, because the shell reacquires on both the CLOSE and the GO HERE path
  // and a double release is a caller mistake that must not be a crash. And a released
  // reader answers next() with false rather than dereferencing a null blocks_.
  OpenChapter card;
  reader::ChapterReader ch;
  REQUIRE(ch.begin(card.fs, card.book.locate(0)));
  reader::Block b;
  REQUIRE(ch.next(b));

  ch.release();
  ch.release();
  CHECK_FALSE(ch.held());
  CHECK_FALSE(ch.inflateWindowHeld());
  CHECK_FALSE(ch.next(b));
}

TEST_CASE("a release before anything was begun is legal, and so is one after a rewind") {
  // The first half is the state a freshly constructed reader is already in: held() is
  // false before begin(), so release() there has nothing to do and must not object.
  reader::ChapterReader fresh;
  CHECK_FALSE(fresh.held());
  fresh.release();
  CHECK_FALSE(fresh.held());

  // The second half is the one that costs something if it is wrong: rewind() keeps the
  // file handle when it has one and REOPENS when it does not, so a released reader is
  // the path through that branch -- and it has to reach the same blocks.
  OpenChapter card;
  reader::ChapterReader ch;
  REQUIRE(ch.begin(card.fs, card.book.locate(0)));
  const std::vector<std::string> before = drain(ch);
  REQUIRE(before.size() >= 1);

  ch.release();
  REQUIRE(ch.rewind());
  CHECK(ch.held());
  CHECK(ch.position() == 0);
  CHECK(drain(ch) == before);
}
