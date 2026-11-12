// THE EXTRACT: the short piece of sentence a Mentions row shows, and the write-once
// parts it lives in.
//
// The property that matters most here is the one the board states and the spec calls
// out: THE WINDOW IS CENTRED ON THE NAME. 64 bytes taken from the front of a mean
// 125-byte sentence frequently stops before the name appears, which is a row that
// does not contain the word it is about.
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/document.h"
#include "reader/name_extracts.h"
#include "reader/names.h"

using reader::Block;
using reader::BlockKind;
using reader::ExtractCapture;
using reader::ExtractPartWriter;
using reader::NameScanner;
using reader::StoredExtract;

namespace {

Block para(std::string text) {
  Block b;
  b.kind = BlockKind::Paragraph;
  b.text = std::move(text);
  return b;
}

bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

}  // namespace

TEST_CASE("the window is centred on the name, not taken from the sentence start") {
  const std::string s =
      "It was a long and rambling sentence of the kind Victorian novelists wrote "
      "without apology, and only here near its very end did Ladislaw appear at all.";
  const size_t at = s.find("Ladislaw");
  REQUIRE(at != std::string::npos);
  const std::string w = reader::extractWindow(s, at, 8);
  CHECK(w.size() <= reader::kExtractBytes);
  // THE WHOLE POINT: the name is in it. Taking 64 bytes off the front of this
  // sentence stops around "novelists", nowhere near the name.
  CHECK(contains(w, "Ladislaw"));
  // ...and there is context on BOTH sides, which is what centring buys.
  CHECK(w.find("Ladislaw") > 0);

  SUBCASE("a sentence shorter than the budget comes back whole") {
    const std::string tiny = "Ladislaw coloured.";
    CHECK(reader::extractWindow(tiny, 0, 8) == "Ladislaw coloured.");
  }
  SUBCASE("a name at the very start still gets a full window") {
    const std::string front =
        "Ladislaw had a sense of the ridiculous which was as keen as anybody could "
        "have wished, and it served him well enough that evening.";
    const std::string w2 = reader::extractWindow(front, 0, 8);
    CHECK(contains(w2, "Ladislaw"));
    CHECK(w2.size() <= reader::kExtractBytes);
  }
  SUBCASE("it never splits a UTF-8 sequence") {
    // Accented text at every offset: a window cut mid-sequence renders as a notdef
    // box, which is worse than a shorter window.
    std::string acc;
    for (int i = 0; i < 20; ++i) acc += "\xC3\xA9t\xC3\xA9 ";
    acc += "Ladislaw ";
    for (int i = 0; i < 20; ++i) acc += "\xC3\xA0 l\xC3\xA0 ";
    const size_t a = acc.find("Ladislaw");
    const std::string w3 = reader::extractWindow(acc, a, 8);
    // Every lead byte must be followed by its continuations: walk it.
    size_t i = 0;
    while (i < w3.size()) {
      const unsigned char c = static_cast<unsigned char>(w3[i]);
      size_t len = 1;
      if (c >= 0xF0) len = 4;
      else if (c >= 0xE0) len = 3;
      else if (c >= 0xC0) len = 2;
      REQUIRE(i + len <= w3.size());
      for (size_t k = 1; k < len; ++k) {
        CHECK((static_cast<unsigned char>(w3[i + k]) & 0xC0) == 0x80);
      }
      i += len;
    }
  }
}

TEST_CASE("the capture is the scanner's own walk, with a different consumer") {
  // A SECOND WALK WITH ITS OWN TOKENISER WOULD BE A SECOND COPY OF THE ELEVEN RULES,
  // free to drift from the counts they produced. So the sink rides addBlock.
  FakeFileSystem fs;
  ExtractPartWriter w(fs, "/.reader/names/abcd1234", 7);
  ExtractCapture cap({"Dorothea", "Ladislaw"}, {8, 8}, w);

  NameScanner s;
  s.addBlock(para("Il regarda Ladislaw longuement ce soir la, puis Dorothea sourit."), 3, &cap);
  s.addBlock(para("Plus tard Ladislaw revint, et Ladislaw repartit aussitot apres."), 4, &cap);
  REQUIRE(w.finish());

  CHECK(cap.kept()[0] == 1);  // Dorothea
  CHECK(cap.kept()[1] == 3);  // Ladislaw

  std::vector<StoredExtract> got;
  REQUIRE(reader::readExtracts(fs, "/.reader/names/abcd1234", 7, "Ladislaw", got));
  REQUIRE(got.size() == 3);
  CHECK(got[0].block == 3);
  CHECK(got[1].block == 4);
  for (const auto& e : got) CHECK(contains(e.text, "Ladislaw"));

  SUBCASE("a run nobody asked for is not stored") {
    std::vector<StoredExtract> none;
    REQUIRE(reader::readExtracts(fs, "/.reader/names/abcd1234", 7, "Casaubon", none));
    CHECK(none.empty());
  }
  SUBCASE("the counts are still kept, so one pass can do both") {
    bool sawLadislaw = false;
    for (const auto& r : s.runs()) {
      if (r.text == "Ladislaw") sawLadislaw = true;
    }
    CHECK(sawLadislaw);
  }
}

TEST_CASE("the quota is per run, and a chapter past it stops capturing") {
  FakeFileSystem fs;
  ExtractPartWriter w(fs, "/.reader/names/abcd1234", 2);
  // Two left for this run, because six are already on the card from earlier chapters.
  ExtractCapture cap({"Ladislaw"}, {2}, w);
  NameScanner s;
  for (int i = 0; i < 5; ++i) {
    // A LOWERCASE WORD BEFORE THE NAME, deliberately. `Alors Ladislaw` is two
    // adjacent capitalised tokens and therefore ONE maximal run, so a fixture
    // written that way captures nothing and says nothing about the quota. The
    // maximal-run rule is not suspended for test text.
    s.addBlock(para("Il regarda Ladislaw encore une fois ce jour la."), i, &cap);
  }
  REQUIRE(w.finish());
  REQUIRE(cap.kept()[0] == 2);
  std::vector<StoredExtract> got;
  REQUIRE(reader::readExtracts(fs, "/.reader/names/abcd1234", 2, "Ladislaw", got));
  REQUIRE(got.size() == 2);
  // The FIRST two, in block order, which is what "the first eight in reading order"
  // means inside one chapter.
  CHECK(got[0].block == 0);
  CHECK(got[1].block == 1);
}

TEST_CASE("the buffer is bounded and flushes to numbered parts") {
  // THERE IS NO STREAMING WRITE: writeAll takes a whole buffer and truncates, and the
  // capture walk holds the 37,056-byte inflate scratch throughout. Repeated writeAll
  // to numbered paths is the only streaming write this filesystem has.
  FakeFileSystem fs;
  ExtractPartWriter w(fs, "/.reader/names/abcd1234", 1, /*partBytes=*/256);
  for (int i = 0; i < 40; ++i) {
    w.add("Ladislaw", i, "a reasonably long extract standing in for a real sentence");
  }
  REQUIRE(w.finish());
  CHECK(w.parts() > 1);
  // ...and every one of them is under the bound, which is the property the number is
  // chosen for.
  for (int n = 0; n < w.parts(); ++n) {
    std::string text;
    std::string path = "/.reader/names/abcd1234/1-" + std::to_string(n);
    REQUIRE(fs.readAll(path, text));
    CHECK(text.size() <= 256 + 128);
  }
  // Read back across the part boundary: all forty, in order.
  std::vector<StoredExtract> got;
  REQUIRE(reader::readExtracts(fs, "/.reader/names/abcd1234", 1, "Ladislaw", got));
  REQUIRE(got.size() == 40);
  for (int i = 0; i < 40; ++i) CHECK(got[static_cast<size_t>(i)].block == i);
}

TEST_CASE("a chapter with nothing to capture writes no part at all") {
  // So an absent part means "nothing here" rather than "not written yet". What
  // distinguishes those is the index's own scanned bit, which is set afterwards.
  FakeFileSystem fs;
  ExtractPartWriter w(fs, "/.reader/names/abcd1234", 5);
  REQUIRE(w.finish());
  CHECK(w.parts() == 0);
  CHECK(!fs.exists("/.reader/names/abcd1234/5-0"));
}

TEST_CASE("a newline in block text cannot split a record") {
  // An EPUB's source is wrapped, so block text genuinely carries newlines. They
  // become spaces rather than being escaped: this is display text and a line break
  // in it is not information.
  FakeFileSystem fs;
  ExtractPartWriter w(fs, "/.reader/names/abcd1234", 0);
  w.add("Ladislaw", 1, "one line\nand another\ttabbed");
  REQUIRE(w.finish());
  std::vector<StoredExtract> got;
  REQUIRE(reader::readExtracts(fs, "/.reader/names/abcd1234", 0, "Ladislaw", got));
  REQUIRE(got.size() == 1);
  CHECK(got[0].text == "one line and another tabbed");
}
