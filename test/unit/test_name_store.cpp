// THE CARD'S SIDE OF THE NAMES FEATURE, and the two properties its correctness rests
// on: a chapter scanned twice must cost nothing, and a merge must not depend on the
// order chapters arrive in.
//
// NEITHER IS AN EDGE CASE. Re-reading is ordinary, and chapters genuinely do arrive
// out of order -- a Contents jump reads 40 before 4, and backfill fills the gap
// afterwards. The scanned-spine bitmap exists for the first and the reading-order
// eviction for the second.
#include <string>
#include <algorithm>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/name_store.h"
#include "reader/names.h"


using reader::NameIndexEntry;
using reader::NameIndexHeader;
using reader::NameScanner;
using reader::NameStore;

namespace {

constexpr const char* kBook = "/books/middlemarch.epub";
constexpr uint32_t kBytes = 123456;

// A chapter's admitted runs, built by hand so the expected counts are countable.
struct Chapter {
  std::vector<NameScanner::Run> runs;
  std::vector<const NameScanner::Run*> ptrs;
  std::vector<int> extracts;

  void add(std::string text, int mid, int opening, int total, int extractCount = 0) {
    NameScanner::Run r;
    r.text = std::move(text);
    r.midSentence = mid;
    r.chapterOpening = opening;
    r.total = total;
    runs.push_back(std::move(r));
    extracts.push_back(extractCount);
  }
  // Sorted by text, which is what NameScanner guarantees and the merge relies on.
  void seal() {
    std::vector<size_t> order(runs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return runs[a].text < runs[b].text; });
    std::vector<NameScanner::Run> sortedRuns;
    std::vector<int> sortedExtracts;
    for (const size_t i : order) {
      sortedRuns.push_back(runs[i]);
      sortedExtracts.push_back(extracts[i]);
    }
    runs = std::move(sortedRuns);
    extracts = std::move(sortedExtracts);
    ptrs.clear();
    for (const auto& r : runs) ptrs.push_back(&r);
  }
};

const NameIndexEntry* find(const std::vector<NameIndexEntry>& v, const std::string& t) {
  for (const auto& e : v) {
    if (e.text == t) return &e;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("a header round-trips, and a version it cannot read is refused whole") {
  NameIndexHeader h;
  h.bookPath = kBook;
  h.bookBytes = kBytes;
  h.admitMidSentence = 2;
  h.extractCap = 8;
  h.markScanned(0);
  h.markScanned(7);
  h.markScanned(91);

  NameIndexHeader back;
  size_t body = 0;
  REQUIRE(reader::parseHeader(reader::serialiseHeader(h), back, &body));
  CHECK(back.bookPath == kBook);
  CHECK(back.bookBytes == kBytes);
  CHECK(back.admitMidSentence == 2);
  CHECK(back.extractCap == 8);
  CHECK(back.isScanned(0));
  CHECK(back.isScanned(7));
  CHECK(back.isScanned(91));
  CHECK(!back.isScanned(1));
  CHECK(back.scannedCount() == 3);
  // The prefix is 1, not 3: chapter 1 is unscanned, so everything above it is a gap.
  CHECK(back.contiguousPrefix() == 1);

  SUBCASE("a future version is discarded rather than parsed hopefully") {
    std::string text = reader::serialiseHeader(h);
    const size_t v = text.find("encre-names\t1");
    REQUIRE(v != std::string::npos);
    text[v + 12] = '9';
    NameIndexHeader bad;
    CHECK(!reader::parseHeader(text, bad, nullptr));
  }
  SUBCASE("an unknown key is skipped, so a later version's field costs nothing") {
    std::string text = reader::serialiseHeader(h);
    text.insert(text.find("path\t"), "future-field\tsomething\n");
    NameIndexHeader ok;
    CHECK(reader::parseHeader(text, ok, nullptr));
    CHECK(ok.bookPath == kBook);
  }
}

TEST_CASE("an entry round-trips, extract locations included") {
  NameIndexEntry e;
  e.text = "Stuart Redman";
  e.midSentence = 41;
  e.chapterOpening = 2;
  e.total = 57;
  e.extracts = {{3, 2}, {9, 1}, {40, 5}};
  NameIndexEntry back;
  REQUIRE(reader::parseEntry(reader::serialiseEntry(e), back));
  CHECK(back.text == "Stuart Redman");
  CHECK(back.midSentence == 41);
  CHECK(back.chapterOpening == 2);
  CHECK(back.total == 57);
  REQUIRE(back.extracts.size() == 3);
  CHECK(back.extracts[0].spine == 3);
  CHECK(back.extracts[2].count == 5);
  CHECK(back.extractCount() == 8);

  SUBCASE("a run with no extracts yet is still a valid line") {
    NameIndexEntry bare;
    bare.text = "Lowick";
    bare.midSentence = 6;
    bare.total = 6;
    NameIndexEntry b2;
    REQUIRE(reader::parseEntry(reader::serialiseEntry(bare), b2));
    CHECK(b2.text == "Lowick");
    CHECK(b2.extracts.empty());
  }
}

TEST_CASE("scanning a chapter twice yields the same index as scanning it once") {
  // THE SCANNED-SPINE BITMAP'S ENTIRE JOB, and the property that fails loudly if it
  // is dropped: without it, re-reading a chapter inflates every mention and admits
  // runs that had been correctly rejected.
  FakeFileSystem fs;
  NameStore store(fs, kBook, kBytes);
  Chapter c;
  c.add("Casaubon", 4, 0, 5);
  c.add("Dorothea", 9, 1, 11);
  c.seal();

  REQUIRE(store.mergeChapter(3, c.ptrs, &c.extracts));
  NameIndexHeader h1;
  std::vector<NameIndexEntry> once;
  REQUIRE(store.loadAll(h1, once));

  // The same chapter again, which is what a re-read does.
  REQUIRE(store.mergeChapter(3, c.ptrs, &c.extracts));
  NameIndexHeader h2;
  std::vector<NameIndexEntry> twice;
  REQUIRE(store.loadAll(h2, twice));

  REQUIRE(once.size() == twice.size());
  for (size_t i = 0; i < once.size(); ++i) {
    CHECK(once[i].text == twice[i].text);
    CHECK(once[i].midSentence == twice[i].midSentence);
    CHECK(once[i].total == twice[i].total);
  }
  CHECK(find(twice, "Dorothea")->midSentence == 9);
}

TEST_CASE("a merge is order-independent, extract lists included") {
  // RE-READING DOES NOT HAPPEN IN SPINE ORDER AND NEITHER DOES BACKFILL. A Contents
  // jump reads 40 before 4 and the gap is filled afterwards, so the index must not
  // depend on which chapter reached it first.
  auto build = [](const std::vector<int>& order) {
    FakeFileSystem fs;
    NameStore store(fs, kBook, kBytes);
    for (const int spine : order) {
      Chapter c;
      // The same run in every chapter, with that chapter's own counts, so the
      // accumulated figure is a sum and the extract list is a choice.
      c.add("Ladislaw", spine + 1, 0, spine + 1, 3);
      c.seal();
      REQUIRE(store.mergeChapter(spine, c.ptrs, &c.extracts));
    }
    NameIndexHeader h;
    std::vector<NameIndexEntry> out;
    REQUIRE(store.loadAll(h, out));
    return out;
  };
  const std::vector<NameIndexEntry> forward = build({1, 2, 3});
  const std::vector<NameIndexEntry> backward = build({3, 2, 1});
  REQUIRE(forward.size() == 1);
  REQUIRE(backward.size() == 1);
  CHECK(forward[0].midSentence == backward[0].midSentence);
  CHECK(forward[0].total == backward[0].total);
  REQUIRE(forward[0].extracts.size() == backward[0].extracts.size());
  for (size_t i = 0; i < forward[0].extracts.size(); ++i) {
    CHECK(forward[0].extracts[i].spine == backward[0].extracts[i].spine);
    CHECK(forward[0].extracts[i].count == backward[0].extracts[i].count);
  }
  // ...and it kept the FIRST eight in reading order: chapters 1 and 2 in full, then
  // two of chapter 3's three. A later chapter arriving first does not get to keep
  // them, which is the rule that preserves a name's introduction.
  CHECK(forward[0].extractCount() == 8);
  CHECK(forward[0].extracts[0].spine == 1);
  CHECK(forward[0].extracts.back().spine == 3);
  CHECK(forward[0].extracts.back().count == 2);
}

TEST_CASE("an index for another book is discarded rather than merged into") {
  // An index for a different book is worse than none, which is the reading
  // position's own answer to the same question.
  FakeFileSystem fs;
  Chapter c;
  c.add("Dorothea", 5, 0, 5);
  c.seal();
  NameStore first(fs, kBook, kBytes);
  REQUIRE(first.mergeChapter(0, c.ptrs, &c.extracts));

  SUBCASE("the bytes disagreeing starts over") {
    NameStore rebound(fs, kBook, kBytes + 1);
    NameIndexHeader h;
    CHECK(!rebound.loadHeader(h));
    // ...and merging rebuilds rather than accumulating on top of a stale count.
    REQUIRE(rebound.mergeChapter(0, c.ptrs, &c.extracts));
    std::vector<NameIndexEntry> out;
    REQUIRE(rebound.loadAll(h, out));
    CHECK(find(out, "Dorothea")->midSentence == 5);
  }
  SUBCASE("a different book gets a different directory entirely") {
    const NameStore other(fs, "/books/other.epub", kBytes);
    CHECK(other.dir() != first.dir());
  }
}

TEST_CASE("a corrupt line costs its own row and not the other seven hundred") {
  FakeFileSystem fs;
  NameStore store(fs, kBook, kBytes);
  Chapter c;
  c.add("Brooke", 3, 0, 3);
  c.add("Casaubon", 4, 0, 4);
  c.add("Dorothea", 5, 0, 5);
  c.seal();
  REQUIRE(store.mergeChapter(0, c.ptrs, &c.extracts));

  std::string text;
  REQUIRE(fs.readAll(store.indexPath(), text));
  const size_t at = text.find("Casaubon");
  REQUIRE(at != std::string::npos);
  text.replace(at, std::string("Casaubon").size(), "@@@@@@@@");
  const size_t tab = text.find('\t', at);
  REQUIRE(tab != std::string::npos);
  text.replace(tab, 1, "!");  // no separator, so the line cannot parse
  REQUIRE(fs.writeAll(store.indexPath(), text));

  NameIndexHeader h;
  std::vector<NameIndexEntry> out;
  REQUIRE(store.loadAll(h, out));
  CHECK(find(out, "Brooke") != nullptr);
  CHECK(find(out, "Dorothea") != nullptr);
  CHECK(find(out, "Casaubon") == nullptr);
}

TEST_CASE("the store is deleted with its book") {
  FakeFileSystem fs;
  NameStore store(fs, kBook, kBytes);
  Chapter c;
  c.add("Dorothea", 5, 0, 5);
  c.seal();
  REQUIRE(store.mergeChapter(0, c.ptrs, &c.extracts));
  REQUIRE(fs.exists(store.indexPath()));
  store.remove();
  CHECK(!fs.exists(store.indexPath()));
}
