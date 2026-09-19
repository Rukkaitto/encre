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

  REQUIRE(store.mergeChapter(3, c.runs, &c.extracts));
  NameIndexHeader h1;
  std::vector<NameIndexEntry> once;
  REQUIRE(store.loadAll(h1, once));

  // The same chapter again, which is what a re-read does.
  REQUIRE(store.mergeChapter(3, c.runs, &c.extracts));
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

TEST_CASE("a merge is order-independent for runs that clear the bar on their own") {
  // RE-READING DOES NOT HAPPEN IN SPINE ORDER AND NEITHER DOES BACKFILL. A Contents
  // jump reads 40 before 4 and the gap is filled afterwards, so the index must not
  // depend on which chapter reached it first.
  //
  // FOR THESE RUNS. The unqualified claim is FALSE and the case below is the one
  // that shows it -- this fixture's run clears the admission bar in every chapter,
  // which is exactly the condition under which order stops mattering.
  auto build = [](const std::vector<int>& order) {
    FakeFileSystem fs;
    NameStore store(fs, kBook, kBytes);
    for (const int spine : order) {
      Chapter c;
      // The same run in every chapter, with that chapter's own counts, so the
      // accumulated figure is a sum and the extract list is a choice.
      c.add("Ladislaw", spine + 1, 0, spine + 1, 3);
      c.seal();
      REQUIRE(store.mergeChapter(spine, c.runs, &c.extracts));
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

TEST_CASE("a chapter past the cap is dropped entirely, not kept at zero") {
  // WRITTEN BECAUSE A MUTATION FAILED NOTHING. Removing the trim left every earlier
  // test passing: with three chapters of three extracts the per-entry clamp already
  // holds the TOTAL at eight, so the branch that drops a whole chapter was never
  // reached. That is the input being too small, not the assertions being too weak --
  // so here are more chapters than the cap can hold, one extract each.
  FakeFileSystem fs;
  NameStore store(fs, kBook, kBytes);
  for (int spine = 0; spine < 12; ++spine) {
    Chapter c;
    c.add("Ladislaw", 2, 0, 2, 1);
    c.seal();
    REQUIRE(store.mergeChapter(spine, c.runs, &c.extracts));
  }
  NameIndexHeader h;
  std::vector<NameIndexEntry> out;
  REQUIRE(store.loadAll(h, out));
  REQUIRE(out.size() == 1);
  // Eight extracts in eight entries -- chapters 0 to 7. Chapters 8 to 11 are absent
  // rather than present with a count of zero: a zero-count entry is a file open that
  // finds nothing, and the list is what tells the screen where to look.
  CHECK(out[0].extractCount() == 8);
  REQUIRE(out[0].extracts.size() == 8);
  CHECK(out[0].extracts.front().spine == 0);
  CHECK(out[0].extracts.back().spine == 7);
  for (const auto& a : out[0].extracts) CHECK(a.count > 0);
  // ...and the counts still accumulated across all twelve, which is the point of
  // admission at the door: the extract list is capped, the figure is not.
  CHECK(out[0].midSentence == 24);
}

TEST_CASE("a run already on the card accumulates a single later mention") {
  // "A RUN THAT CLEARS THE BAR KEEPS ACCUMULATING FOR THE REST OF THE BOOK" is the
  // whole of what admission at the door buys over eviction by count. It was BROKEN
  // and the unit tests were all green: admission was decided by the scanner, which
  // cannot see the index, so a run already on the card that appeared once in a later
  // chapter contributed nothing. Found by running the pipeline over a real novel and
  // noticing 155 missing extracts, not here.
  FakeFileSystem fs;
  NameStore store(fs, kBook, kBytes);

  Chapter first;
  first.add("Ladislaw", 4, 0, 4, 2);   // admitted: two or more mid-sentence
  first.add("Casaubon", 1, 0, 1, 1);   // NOT admitted: one is under the bar
  first.seal();
  REQUIRE(store.mergeChapter(0, first.runs, &first.extracts));

  NameIndexHeader h;
  std::vector<NameIndexEntry> out;
  REQUIRE(store.loadAll(h, out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].text == "Ladislaw");

  Chapter second;
  second.add("Ladislaw", 1, 0, 1, 1);  // ONE mention, and it must still count
  second.add("Casaubon", 1, 0, 1, 1);  // still under the bar, still refused
  second.seal();
  REQUIRE(store.mergeChapter(1, second.runs, &second.extracts));
  REQUIRE(store.loadAll(h, out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].text == "Ladislaw");
  CHECK(out[0].midSentence == 5);       // 4 + 1, not 4
  CHECK(out[0].extractCount() == 3);    // 2 + 1, not 2

  SUBCASE("...and the bar still applies to a run that has never cleared it") {
    Chapter third;
    third.add("Casaubon", 2, 0, 2, 1);  // NOW it clears it, on its own chapter
    third.seal();
    REQUIRE(store.mergeChapter(2, third.runs, &third.extracts));
    REQUIRE(store.loadAll(h, out));
    REQUIRE(out.size() == 2);
    // It enters with THIS chapter's counts only. The two earlier single mentions are
    // gone, which is the stated price of a bounded index: a name appearing once per
    // chapter across many chapters is never admitted.
    CHECK(find(out, "Casaubon")->midSentence == 2);
  }
}

TEST_CASE("admission is order-dependent, and that is a bound rather than a defect") {
  // THE SPEC SAID "A MERGE IS ORDER-INDEPENDENT" FLATLY, AND IT IS NOT. Measured by
  // running the real pipeline over a real novel twice -- once reading chapters 0..24
  // in order, once jumping to 24 and backfilling 0..23 forward -- the two indexes
  // agree on 156 of 171 entries and differ by one or two mentions on the other 15,
  // in BOTH directions. The group count came to 61 against 62.
  //
  // THE MECHANISM IS ADMISSION MEETING ORDER. A run is admitted only when one
  // chapter alone sees it twice mid-sentence, and mentions seen BEFORE that are
  // dropped -- so which chapter arrives first decides which singles survive.
  //
  // IT CANNOT BE FIXED WITHOUT GIVING UP THE BOUND. Keeping an unadmitted run's
  // count is the 85,001-byte whole-book table the entire design exists to refuse.
  // So it is stated here, pinned, and accepted: a name may sit one mention either
  // side of the display threshold depending on the route a reader took through the
  // book, which is not something a reader can see.
  FakeFileSystem fs;

  // One mention in the early chapter, two in the later one.
  Chapter early;
  early.add("Ezwick", 1, 0, 1);
  early.seal();
  Chapter late;
  late.add("Ezwick", 2, 0, 2);
  late.seal();

  NameStore readThrough(fs, kBook, kBytes);
  REQUIRE(readThrough.mergeChapter(5, early.runs, nullptr));   // dropped: under the bar
  REQUIRE(readThrough.mergeChapter(24, late.runs, nullptr));   // admitted here
  NameIndexHeader h;
  std::vector<NameIndexEntry> out;
  REQUIRE(readThrough.loadAll(h, out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].midSentence == 2);  // the ch5 single never made it

  NameStore jumped(fs, "/books/other.epub", kBytes);
  REQUIRE(jumped.mergeChapter(24, late.runs, nullptr));   // admitted first
  REQUIRE(jumped.mergeChapter(5, early.runs, nullptr));   // ...so this one counts
  std::vector<NameIndexEntry> out2;
  REQUIRE(jumped.loadAll(h, out2));
  REQUIRE(out2.size() == 1);
  CHECK(out2[0].midSentence == 3);

  // THE TWO DISAGREE, DELIBERATELY. Asserting the difference rather than the
  // equality is what stops someone "fixing" this into the unbounded table.
  CHECK(out[0].midSentence != out2[0].midSentence);
}

TEST_CASE("an index for another book is discarded rather than merged into") {
  // An index for a different book is worse than none, which is the reading
  // position's own answer to the same question.
  FakeFileSystem fs;
  Chapter c;
  c.add("Dorothea", 5, 0, 5);
  c.seal();
  NameStore first(fs, kBook, kBytes);
  REQUIRE(first.mergeChapter(0, c.runs, &c.extracts));

  SUBCASE("the bytes disagreeing starts over") {
    NameStore rebound(fs, kBook, kBytes + 1);
    NameIndexHeader h;
    CHECK(!rebound.loadHeader(h));
    // ...and merging rebuilds rather than accumulating on top of a stale count.
    REQUIRE(rebound.mergeChapter(0, c.runs, &c.extracts));
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
  REQUIRE(store.mergeChapter(0, c.runs, &c.extracts));

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
  REQUIRE(store.mergeChapter(0, c.runs, &c.extracts));
  REQUIRE(fs.exists(store.indexPath()));
  store.remove();
  CHECK(!fs.exists(store.indexPath()));
}
