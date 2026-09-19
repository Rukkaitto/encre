// THE SCAN'S RULES, so the ones the probe paid an iteration each for cannot drift.
//
// `tools/name_probe.cpp` is the reference and its header is the record of what each
// rule fixed. These are the six of its eleven that live in `core/names.cpp`; the
// other five are grouping -- containment, prefix edges, the plural and dominance
// guards, the fuller-name tie-break, earliest-of-best-tier -- and they arrive with
// the display-time pass that performs them.
//
// EVERY FIXTURE HERE IS SYNTHETIC AND EVERY EXPECTED NUMBER IS COUNTABLE BY HAND.
// That is deliberate and it is also the limit: the two porting bugs this file's
// subject was written with were BOTH invisible to synthetic text and were found by
// running the scanner over a real 1,400-page novel and comparing with the probe.
// A rule can be pinned here; an ARITHMETIC that drifts by 2% over 92 chapters cannot.
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/document.h"
#include "reader/heapguard.h"
#include "reader/names.h"

using reader::Block;
using reader::BlockKind;
using reader::NameScanner;

namespace {

Block para(std::string text) {
  Block b;
  b.kind = BlockKind::Paragraph;
  b.text = std::move(text);
  return b;
}

// The scanner over one block, which is the shape most of these want.
NameScanner scanOne(const std::string& text, int blockInChapter = 5) {
  NameScanner s;
  const Block b = para(text);
  s.addBlock(b, blockInChapter);
  return s;
}

const NameScanner::Run* runNamed(const NameScanner& s, const std::string& text) {
  for (const auto& r : s.runs()) {
    if (r.text == text) return &r;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("a run's rank is its mid-sentence mentions, which is the load-bearing rule") {
  // RANKING ON TOTAL MENTIONS RETURNS A WORD-FREQUENCY TABLE. A French pronoun starts
  // thousands of sentences, so `Il` outranks every character in the book -- this one
  // rule is the difference between a cast list and noise.
  const NameScanner s = scanOne("Elle partit. Elle revint. Stu parla a Elle longtemps.");
  const auto* elle = runNamed(s, "Elle");
  REQUIRE(elle != nullptr);
  CHECK(elle->total == 3);
  // Two of the three open a sentence and are suppressed; only the third counts.
  CHECK(elle->midSentence == 1);
  // ...and `Stu`, which opens its sentence, scores zero however often it appears.
  const auto* stu = runNamed(s, "Stu");
  REQUIRE(stu != nullptr);
  CHECK(stu->total == 1);
  CHECK(stu->midSentence == 0);
}

TEST_CASE("a short capitalised word before a period is not an abbreviation") {
  // NAMES ARE SHORT, so guarding on length alone means "parla a Stu. Je crois" never
  // splits -- and every following word then counts as mid-sentence, which is how the
  // pronouns got in. Only a single-letter initial, or one of a dozen listed forms.
  SUBCASE("a short name still ends its sentence") {
    const NameScanner s = scanOne("Il parla a Stu. Je crois que oui.");
    const auto* je = runNamed(s, "Je");
    // `Je` opens the second sentence, so it must NOT count as mid-sentence.
    REQUIRE(je != nullptr);
    CHECK(je->midSentence == 0);
  }
  SUBCASE("a single-letter initial does not") {
    // `J. Cassini` is one sentence, so `Cassini` is genuinely mid-sentence.
    const NameScanner s = scanOne("Voici J. Cassini qui arrive.");
    const auto* c = runNamed(s, "Cassini");
    REQUIRE(c != nullptr);
    CHECK(c->midSentence == 1);
  }
  SUBCASE("a listed abbreviation does not") {
    // A lowercase word in front, so the run under test does NOT start the sentence:
    // if the guard failed and `Mme.` split, `Bovary ce matin.` would be a new
    // sentence and its opening run would score zero. That is the whole difference
    // the rule makes, and asserting it needs a run that could go either way.
    const NameScanner s = scanOne("Il salua Mme. Bovary ce matin.");
    const auto* b = runNamed(s, "Mme Bovary");
    REQUIRE(b != nullptr);
    CHECK(b->midSentence == 1);
    // And the period really did not split it: `Bovary` is not a run of its own,
    // because the three capitalised tokens either side of the period are ONE
    // maximal run. That is the maximal-run rule and the guard agreeing.
    CHECK(runNamed(s, "Bovary") == nullptr);
  }
}

TEST_CASE("an entity is a maximal run, counted once") {
  // COUNTING EACH TOKEN SEPARATELY AS WELL put `La` in the list beside `La Poubelle`
  // with 316 mentions of its own, because every "La Poubelle" was also a "La".
  const NameScanner s = scanOne("Ils virent La Poubelle et La Poubelle repartit.");
  CHECK(runNamed(s, "La") == nullptr);
  CHECK(runNamed(s, "Poubelle") == nullptr);
  const auto* lp = runNamed(s, "La Poubelle");
  REQUIRE(lp != nullptr);
  CHECK(lp->total == 2);

  SUBCASE("a comma ends a run, so a list is not one entity") {
    const NameScanner s2 = scanOne("Ils virent Stu, Larry et Glen partir ensemble.");
    CHECK(runNamed(s2, "Stu") != nullptr);
    CHECK(runNamed(s2, "Larry Glen") == nullptr);
  }
}

TEST_CASE("an apostrophe's freight is trimmed at both ends") {
  // English possessives and contractions made `I'm` and `Deborah's` read as names;
  // French elision made `d'Amy` read as `D'Amy`. One rule each, no vocabulary.
  SUBCASE("a trailing possessive goes") {
    const NameScanner s = scanOne("He took Deborah's coat from the hook.");
    CHECK(runNamed(s, "Deborah") != nullptr);
    CHECK(runNamed(s, "Deborah's") == nullptr);
  }
  SUBCASE("a leading elision goes") {
    const NameScanner s = scanOne("Il parla d'Amy toute la soiree.");
    CHECK(runNamed(s, "Amy") != nullptr);
  }
  SUBCASE("a long tail is not a contraction") {
    // `O'Brien` survives as `Brien`: the O is lost, which is the stated price, and a
    // five-letter tail is not a contraction.
    const NameScanner s = scanOne("He saw O'Brien leave the room.");
    CHECK(runNamed(s, "Brien") != nullptr);
  }
}

TEST_CASE("NBSP is whitespace for splitting, and the pronoun leak was never lexical") {
  // French typography sets a non-breaking space inside guillemets and before ! ? :,
  // so a quoted sentence really reads "«Bonjour ! »". Skipping
  // only ASCII spaces meant the splitter stopped dead on the NBSP, never reached the
  // closing guillemet and never split -- so the NEXT sentence's opening word counted
  // as mid-sentence. Fixing it removed Il and Elle from Neuromancien's list entirely.
  const std::string nbsp = "\xC2\xA0";
  const std::string guilOpen = "\xC2\xAB";
  const std::string guilClose = "\xC2\xBB";
  const NameScanner s =
      scanOne(guilOpen + nbsp + "Bonjour" + nbsp + "!" + nbsp + guilClose + " Elle partit.");
  const auto* elle = runNamed(s, "Elle");
  REQUIRE(elle != nullptr);
  CHECK(elle->total == 1);
  // It opens the second sentence. Without NBSP-as-whitespace the split never happened
  // and this read as mid-sentence.
  CHECK(elle->midSentence == 0);
}

TEST_CASE("a word opening reported speech is sentence-initial wherever it sits") {
  // `Elle disait : "Je n'arrive pas a respirer"` puts `Je` mid-sentence by POSITION
  // and first-word by GRAMMAR. Only a quote or a colon counts -- deliberately not a
  // dash or a paren, since French sets parenthetical em-dashes mid-sentence and
  // suppressing a real name there could push it under the threshold.
  SUBCASE("a colon opens speech") {
    const NameScanner s = scanOne("Elle disait : Je crois que non.");
    const auto* je = runNamed(s, "Je");
    REQUIRE(je != nullptr);
    CHECK(je->total == 1);
    CHECK(je->midSentence == 0);
  }
  SUBCASE("a quote opens speech") {
    const NameScanner s = scanOne("Elle disait \"Je crois que non\" ce soir.");
    const auto* je = runNamed(s, "Je");
    REQUIRE(je != nullptr);
    CHECK(je->midSentence == 0);
  }
  SUBCASE("a dash does NOT, because French sets parentheticals with one") {
    // "les trois - Stu, Larry et Glen - partirent": suppressing Stu here would cost a
    // real name a mention for standing after punctuation.
    const NameScanner s = scanOne("Les trois - Stu, Larry et Glen - partirent ensemble.");
    const auto* stu = runNamed(s, "Stu");
    REQUIRE(stu != nullptr);
    CHECK(stu->midSentence == 1);
  }
}

TEST_CASE("the furniture cut is positional, and it is NOT applied per chapter") {
  // A RUNNING HEADER IS NOT A CHARACTER, and the position finds it where the tag does
  // not: skipping Heading blocks is the obvious fix and is a no-op, because two of
  // three real EPUBs contain no h1-h6 at all and real books put chapter titles in
  // <p class="...">.
  NameScanner s;
  s.addBlock(para("Darkly Dreaming Dexter"), 0);
  s.addBlock(para("Il regarda Dexter puis Dexter sourit longuement."), 7);
  const auto* title = runNamed(s, "Darkly Dreaming Dexter");
  REQUIRE(title != nullptr);
  CHECK(title->chapterOpening == 1);
  CHECK(title->total == 1);
  const auto* dex = runNamed(s, "Dexter");
  REQUIRE(dex != nullptr);
  CHECK(dex->chapterOpening == 0);

  // THE COUNTS GO ON THE CARD AND THE VERDICT DOES NOT. Applying the cut here was a
  // real bug: over a whole book it identifies the running header and removes one run,
  // but asked per chapter any run appearing only in a chapter's first two blocks
  // scores 100% -- it threw away 30 of Le Fleau's 738 admitted runs. So `admitted`
  // filters on mid-sentence mentions alone and the cut happens over accumulated
  // figures.
  NameScanner t;
  t.addBlock(para("Elle vit Amy. Puis elle vit Amy encore une fois."), 0);
  const auto adm = t.admitted();
  bool sawAmy = false;
  for (const auto* r : adm) {
    if (r->text == "Amy") sawAmy = true;
  }
  const auto* amy = runNamed(t, "Amy");
  REQUIRE(amy != nullptr);
  CHECK(amy->midSentence == 2);
  CHECK(amy->chapterOpening == amy->total);  // every mention opened the chapter
  CHECK(sawAmy);                             // ...and it is admitted anyway
}

TEST_CASE("admission is two mid-sentence mentions in one chapter") {
  // ADMISSION AT THE DOOR RATHER THAN EVICTION AFTER THE FACT. Eviction by count
  // thrashes: a name at two mentions is dropped, reappears with two more and is stored
  // as two again, losing history for exactly the mid-frequency names the feature
  // exists to serve.
  NameScanner s;
  s.addBlock(para("Il vit Amy. Il revit Amy plus tard. Il croisa Bob une seule fois."), 3);
  const auto adm = s.admitted();
  REQUIRE(adm.size() == 1);
  CHECK(adm[0]->text == "Amy");
  CHECK(adm[0]->midSentence == 2);
  // Bob is seen once mid-sentence and is NOT admitted -- but it is still counted, so a
  // later chapter can admit it.
  const auto* bob = runNamed(s, "Bob");
  REQUIRE(bob != nullptr);
  CHECK(bob->midSentence == 1);

  SUBCASE("the threshold is a parameter, so the header can state the one in force") {
    CHECK(s.admitted(1).size() == 2);
    CHECK(s.admitted(3).empty());
  }
}

TEST_CASE("the table is bounded, and a chapter that overflows says so") {
  // A BOUND THIS FILE CANNOT AFFORD IS NOT A BOUND. 512 against a worst measured
  // chapter of 412, and a silent truncation would be a count quietly wrong for the
  // rest of the book -- BookList reports what it dropped for the same reason.
  NameScanner s;
  std::string text;
  for (int i = 0; i < NameScanner::kMaxRuns + 40; ++i) {
    text += "Il vit Aa" + std::to_string(i) + "bb maintenant. ";
  }
  s.addBlock(para(text), 4);
  CHECK(static_cast<int>(s.runs().size()) == NameScanner::kMaxRuns);
  CHECK(s.dropped() > 0);

  SUBCASE("a run longer than the cap is not a name and is never stored") {
    std::string longRun = "Il vit";
    for (int i = 0; i < 20; ++i) longRun += " Abcdef";
    longRun += " maintenant.";
    NameScanner t;
    t.addBlock(para(longRun), 4);
    for (const auto& r : t.runs()) CHECK(r.text.size() <= NameScanner::kMaxRunBytes);
  }
}

TEST_CASE("runs come out sorted by text, which is the merge's order and not the screen's") {
  // The file is ordered by RUN name, which makes the per-chapter merge a linear
  // two-way merge rather than a sort. The LIST is ordered by group display name,
  // which only exists after grouping -- so a screen must not assume this order.
  const NameScanner s =
      scanOne("Il vit Zoe puis Amy puis Marc, et Amy revint avec Marc ensuite.");
  REQUIRE(s.runs().size() >= 3);
  for (size_t i = 1; i < s.runs().size(); ++i) {
    CHECK(s.runs()[i - 1].text < s.runs()[i].text);
  }
}

TEST_CASE("a heap that cannot grow the table drops runs rather than aborting") {
  // REPORTED OFF GLASS AS A REBOOT TO HOME MID-BOOK, and the dump named findOrAdd:
  // operator new threw inside _M_realloc_insert and -fno-exceptions turned it into
  // a terminate. The device had 41,616 bytes free and a largest BLOCK of 14,324 --
  // the free heap said yes and the only number that decides an allocation said no.
  //
  // THE DESKTOP CANNOT FAIL AN ALLOCATION NATURALLY, which is what Heap::install is
  // for: failure is injected exactly as Profile injects a clock.
  struct Guard {
    ~Guard() { reader::Heap::install(nullptr); }
  } restore;

  // Room for a handful and no more.
  reader::Heap::install([](size_t bytes) { return bytes <= 6 * sizeof(NameScanner::Run); });

  NameScanner s;
  std::string text;
  for (int i = 0; i < 60; ++i) text += "Il vit Aa" + std::to_string(i) + "bb maintenant. ";
  s.addBlock(para(text), 4);

  // IT KEPT WHAT IT COULD AND SAID WHAT IT DID NOT. The table is already bounded and
  // already reports drops, so a chapter scanned on a fragmented heap keeps fewer
  // names -- where the alternative is losing the reader's page.
  CHECK(!s.runs().empty());
  CHECK(static_cast<int>(s.runs().size()) < 60);
  CHECK(s.dropped() > 0);

  SUBCASE("a sentence that cannot be tokenised is abandoned whole, not half") {
    // THE CONTAINER THAT ACTUALLY CRASHED THE DEVICE, twice, both times at chapter
    // 29: `toks` is cleared per sentence but keeps its capacity, so it reallocates
    // only for a NEW longest sentence -- exactly the rare, unpredictable growth a
    // fragmented heap refuses. Guarding the runs table left this one open.
    //
    // ABANDONED WHOLE, because a partial token list produces runs that are not in
    // the book: a maximal run is the span between two non-candidates, and cutting
    // the list mid-sentence invents a boundary.
    reader::Heap::install([](size_t bytes) { return bytes <= 4 * sizeof(void*); });
    NameScanner t;
    t.addBlock(para("Il vit Amy et Bob et Cal et Dan et Eve ensemble maintenant."), 4);
    CHECK(t.dropped() > 0);
    reader::Heap::install(nullptr);
  }

  SUBCASE("a run longer than the cap is never BUILT, not built and then measured") {
    // The length check protected the TABLE and not the heap: the run string was
    // assembled from every token first and rejected afterwards, so a line of
    // capitalised words allocated a string of any size before being thrown away.
    NameScanner t;
    std::string line = "Il vit";
    for (int i = 0; i < 40; ++i) line += " Abcdefghij";
    line += " maintenant.";
    t.addBlock(para(line), 4);
    for (const auto& r : t.runs()) CHECK(r.text.size() <= NameScanner::kMaxRunBytes);
  }

  SUBCASE("and a sink still sees every occurrence, table or no table") {
    // The capture pass runs on the tightest heap there is and wants occurrences
    // rather than counts, so it builds no table at all.
    struct Sink : NameScanner::RunSink {
      int n = 0;
      void onRun(std::string_view, int, std::string_view, size_t, bool) override { ++n; }
    } sink;
    NameScanner t;
    t.setSinkOnly(true);
    t.addBlock(para(text), 4);
    CHECK(t.runs().empty());   // nothing was kept...
    t.addBlock(para(text), 4, &sink);
    CHECK(sink.n > 0);         // ...and the sink was fed anyway
    CHECK(t.runs().empty());
  }
}

TEST_CASE("reset clears the chapter, so one scanner serves a whole book") {
  NameScanner s;
  s.addBlock(para("Il vit Amy. Il revit Amy plus tard."), 2);
  REQUIRE(!s.runs().empty());
  s.reset();
  CHECK(s.runs().empty());
  CHECK(s.dropped() == 0);
}
