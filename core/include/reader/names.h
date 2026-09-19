#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/document.h"

namespace reader {

// NAMES, out of a real book, with nothing but capitalisation and arithmetic.
//
// This is `tools/name_probe.cpp`'s heuristic moved into `core/` so the device can run
// it. The probe stays: it is the thing you point at a new novel, and its header
// carries the eleven rules with the failure each one fixed. THIS FILE IS THE SAME
// RULES AND A DIFFERENT MEMORY BUDGET -- the probe holds every candidate in the book
// because a desktop can, and a whole-book table is 85,001 bytes against a measured
// 45,840-byte floor.
//
// --- WHAT IT COUNTS, AND WHY THAT IS THE WHOLE TRICK ---------------------------
//
// A run's rank is its MID-SENTENCE mentions: the ones that are neither the first word
// of a sentence nor the first word of reported speech. Ranking on total mentions
// returns a word-frequency table, because a French pronoun starts thousands of
// sentences -- `Il`, `Je`, `Et`, `Elle`. That one rule is the difference between a
// cast list and noise, and everything else here exists to make the sentence
// boundaries accurate enough for it to work.
//
// --- THE BOUND -----------------------------------------------------------------
//
// ONE CHAPTER AT A TIME, and the worst chapter measured over a real novel is 412 runs
// and 7,797 bytes of keys and counters. `kMaxRuns` is 512, and a chapter that exceeds
// it DROPS runs and says how many rather than growing -- BookList's rule, and for its
// reason: a bound this file cannot afford is not a bound, and a silent truncation is
// a count that is quietly wrong for the rest of the book.
class NameScanner {
 public:
  // A capitalised run as ONE chapter saw it.
  struct Run {
    std::string text;
    // Mentions that are neither sentence-initial nor speech-initial. THE RANKING
    // FIGURE, and the only one a threshold is ever applied to.
    int midSentence = 0;
    // Mentions sitting in the chapter's first two blocks. A running header is not a
    // character, and the position finds it where the tag does not: real characters
    // spend 0-7% of their mentions opening a chapter and a book's own title is far
    // above that. Kept per RUN and applied BEFORE grouping, because the title folds
    // into the character sharing its name and dilutes the group's figure.
    int chapterOpening = 0;
    // Every mention, including the suppressed ones. Not a ranking input; it is what
    // `chapterOpening` is a fraction OF.
    int total = 0;
  };

  // SIZED FROM A REAL BOOK rather than from a round number: 412 is the worst chapter
  // of `Le Fleau`, a 1,400-page novel with an enormous cast, and 512 leaves room
  // without doubling the table. At ~19 bytes a run that is ~9.7 KB at the cap.
  static constexpr int kMaxRuns = 512;
  // A run longer than this is not a name. It bounds the table's bytes independently
  // of the run count, which `kMaxRuns` alone does not: one pathological run of a
  // thousand capitalised words would otherwise be a kilobyte on its own.
  static constexpr size_t kMaxRunBytes = 64;

  // WHERE THE CAPTURE PASS HOOKS IN, so there is one walk and not two.
  //
  // The extracts have to be captured on a SECOND walk of the chapter -- admission is
  // only decided at the end of the first, and holding candidate extracts for every
  // run meanwhile is 28 KB beside the 37,056-byte inflate scratch that walk is using.
  // But a second walk with its own tokeniser would be a second copy of the eleven
  // rules, free to drift from the counts they produced. So it is THIS walk with a
  // different consumer: the scanner tokenises, and the sink decides what to keep.
  class RunSink {
   public:
    virtual ~RunSink() = default;
    // `sentence` is the run's own sentence and `offset` is where the run starts in
    // it, which is what an extract centred on the name needs. `midSentence` is
    // whether this occurrence counted -- the sink sees suppressed ones too, because
    // a name's first appearance in a chapter can legitimately open a sentence.
    virtual void onRun(std::string_view run, int blockInChapter, std::string_view sentence,
                       size_t offset, bool midSentence) = 0;
  };

  // Start a chapter. Clears everything, so one scanner serves a whole book.
  void reset();

  // One block of the chapter, in order. `blockInChapter` is its index from 0 --
  // needed for the furniture cut, which is positional and not a tag: two of three
  // real EPUBs contain NO h1-h6 at all, so skipping Heading blocks is a no-op.
  //
  // With a `sink` the counts are still kept, so a caller may do both in one pass on
  // a chapter it has already admitted. The capture pass passes a sink and throws the
  // counts away; the count pass passes none.
  void addBlock(const Block& block, int blockInChapter, RunSink* sink = nullptr);

  // This chapter's runs, SORTED BY TEXT. The order is the merge's, not the screen's.
  const std::vector<Run>& runs() const { return runs_; }

  // How many distinct runs this chapter could not hold. Reported rather than hidden:
  // a chapter that overflowed has counts that are right for what it kept and silent
  // about what it did not.
  int dropped() const { return dropped_; }

  // The runs this chapter ADMITS to the card: those it saw at least `minMidSentence`
  // times mid-sentence. Admission at the door rather than eviction after the fact,
  // because eviction by count thrashes -- a name at two mentions is dropped,
  // reappears with two more and is stored as two again, losing history for exactly
  // the mid-frequency names the feature exists to serve.
  std::vector<const Run*> admitted(int minMidSentence = kAdmitMidSentence) const;

  // TWO AT THE DOOR, measured: it takes `Le Fleau`'s card index from 4,093 runs and
  // 85,001 bytes to 722 and 14,547 -- 17.1% -- which is the difference between a
  // feature and an impossibility. It is stored in the index's header, so changing it
  // invalidates the index rather than mixing two populations into one set of counts.
  //
  // AND IT COUNTS THE SAME `midSentence` THE RANKING DOES, which the probe did not.
  // `name_probe.cpp`'s admission counter checks POSITION only and ignores the opener
  // rule, so it admits 738 runs and 14,812 bytes -- the figure the design was
  // approved on. Porting the scan here surfaced the two definitions, and one wins:
  // a rule that differs from its own name by an invisible clause is the drift this
  // project spends most of its comments on. The 16 runs it costs would not have
  // reached a screen, since the display threshold is five of the strict count.
  static constexpr int kAdmitMidSentence = 2;

 private:
  Run* find(std::string_view text);
  Run* findOrAdd(std::string_view text);

  std::vector<Run> runs_;
  int dropped_ = 0;
};

// --- Grouping ------------------------------------------------------------------
//
// THE SCREEN'S JOB, NOT THE SCAN'S, and the card holds raw runs because of it. The
// probe's grouping is a batch operation over the whole candidate set: doing it
// incrementally per chapter would freeze decisions later chapters should be able to
// change -- `Fran` is unambiguous until `Frank` appears, which may be thirty chapters
// later.
//
// THREE OF THE PROBE'S ELEVEN RULES DO NOT COME ACROSS, and that is the Mentions
// screen's doing rather than an omission. Rules 4, 5 and 8 -- the earliest of the
// best TIER, the lowercase-after-comma test that kills the front matter, and the
// fuller-name tie-break -- all exist to choose ONE introducing sentence per group.
// Nothing chooses one any more: a name opens a list of its first eight sightings and
// the reader picks. The two that do come across are the ones that decide who is WHO.
struct NameGroup {
  // The form the book uses most, which is what the list displays: `Stu`. A reader
  // arrives knowing the string because they just read it on the page.
  std::string display;
  // The longest member, shown under the display name in tracked caps: `STUART
  // REDMAN`. EMPTY when the group has one member or its longest IS its display name
  // -- a place, or a name the book always writes in full -- and an empty one makes
  // the row short rather than drawing a blank line.
  std::string fullest;
  // The group's rank: its members' mid-sentence mentions, summed.
  int midSentence = 0;
  // Every run in the group, most-mentioned first. What a lookup needs to find the
  // group's extracts, which are stored per RUN.
  std::vector<std::string> members;
};

// Group `entries` and return the groups sorted by DISPLAY NAME.
//
// ALPHABETICAL, WHICH INVERTS THE OBVIOUS ORDER. Ranking by mentions puts the leads
// at the top, and you never look up a lead: the name you cannot place is rare, so
// most-mentioned-first buries it hundreds of rows down and moves it as you read on.
//
// `minMidSentence` is the DISPLAY threshold and is a different number from admission's
// -- that one is about what the card keeps so a count can go on growing, this one is
// about how many rows a reader scrolls.
//
// `furnitureCutPercent` drops a run that spends at least that share of its mentions
// opening a chapter. Applied per RUN and BEFORE grouping, because a book's own title
// folds into the character sharing its name and would dilute the group's figure.
std::vector<NameGroup> groupNames(const std::vector<struct NameIndexEntry>& entries,
                                  int minMidSentence = 5, int furnitureCutPercent = 50);

// --- The pieces, exposed because they each earned a test -----------------------
//
// These are internal to the scan and are declared here so the rules the probe paid
// for cannot drift silently. `test_names.cpp` drives them directly; nothing else
// should.
namespace names_detail {

// Trim what an apostrophe glues on, at both ends. `I'm`, `I'd` and `Deborah's` read
// as names in English; `d'Amy` and `l'Enfant` read as names in French. One rule each,
// neither needing a vocabulary. `O'Brien` survives as `Brien`, which is the price.
std::string trimApostrophe(std::string_view token);

// Split on . ! ? and the ellipsis, guarded against abbreviations. A SHORT CAPITALISED
// WORD BEFORE A PERIOD IS NOT AN ABBREVIATION -- names are short, and guarding on
// length alone means "parla a Stu. Je crois" never splits, which is how the pronouns
// got in. Only a single-letter initial, or one of a dozen listed forms.
std::vector<std::string_view> sentences(std::string_view text);

// Whether a sentence opens with dialogue punctuation.
bool dialogueStart(std::string_view s);

}  // namespace names_detail
}  // namespace reader
