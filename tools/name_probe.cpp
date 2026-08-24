// NAMES, out of a real book, with nothing but capitalisation and arithmetic.
//
// This is the probe that answered 3E's riskiest question -- whether a heuristic a
// 160 MHz part could run yields a list worth reading -- before any of 3E was
// planned. It is kept because the ANSWER is a set of five rules that each cost an
// iteration to find, and prose in a spec cannot be re-run on a new book.
//
//   build: c++ -std=c++20 -O2 -I core/include -DREADER_DESKTOP=1 \
//              tools/name_probe.cpp build/libreader_core.a -o build/name_probe
//   run:   build/name_probe <book.epub> [--min N] [--top N]
//
// NOT in ctest, and deliberately: it needs a real novel, and the repo's generated
// EPUBs are 1,400-word stubs where every threshold reports zero. Run it by hand on
// a book you have.
//
// WHAT IT FOUND, over three real novels (Le Fleau, Neuromancien, Darkly Dreaming
// Dexter): the top of the list is the cast. 25 of the top 26 for Le Fleau are real
// named entities -- Stu, Larry, Harold, Nick, Frannie, Flagg, La Poubelle, Kojak the
// dog -- and `Dieu`, which is French for God: a proper noun, always capitalised, and
// so a CORRECT entry on a screen that says NAMES. Neuromancien returns
// Case, Molly, Armitage, Wintermute, Chiba, Tessier-Ashpool.
//
// THE RULES, each of which replaced something that looked reasonable and was wrong.
// They are restated at their sites; this is the index.
//
//  1. SENTENCE-INITIAL SUPPRESSION IS THE LOAD-BEARING RULE. Rank on mentions that
//     are NOT the first word of a sentence. Ranking on total mentions floods the list
//     with Il / Je / Et / Elle -- a French pronoun starts thousands of sentences.
//     This one rule is the difference between a cast list and a word frequency table.
//
//  2. A SHORT CAPITALISED WORD BEFORE A PERIOD IS NOT AN ABBREVIATION. Names are
//     short. Guarding on length alone means "parla a Stu. Je crois" never splits, and
//     every following word then counts as mid-sentence -- which is how the pronouns
//     got in. Only a single-letter initial, or one of a dozen listed abbreviations.
//
//  3. AN ENTITY IS A MAXIMAL RUN, COUNTED ONCE. Counting each token separately as
//     well put `La` in the list beside `La Poubelle` with 316 mentions of its own,
//     because every "La Poubelle" was also being counted as a "La".
//
//  4. THE INTRODUCING SENTENCE IS THE EARLIEST OF THE BEST KIND, NOT THE BEST-SCORING.
//     Scoring a running best over every occurrence and rewarding length selects for
//     the most verbose sentence in the novel: it replaced "Harold, un gros garcon de
//     seize ans" with a ramble. Length is a usable proxy over six samples and a trap
//     over nine hundred. An introduction also happens once, and early.
//
//  5. REQUIRING THE WORD AFTER THE COMMA TO BE LOWERCASE KILLS THE FRONT MATTER FOR
//     FREE. The first occurrences of six major characters are all in the author's
//     preface, and the best-scoring sentence there was one listing eight of them --
//     which introduces none of them. A capital after the comma means a list.
//
//  6. TRIM WHAT AN APOSTROPHE GLUES ON, at both ends. `I'm`, `I'd` and `Deborah's`
//     read as names in English; `d'Amy` and `l'Enfant` read as names in French.
//
//  7. THE CONTAINMENT LINK TAKES NO RATIO GUARD, and this was the single largest
//     improvement in the whole probe. The guard existed so `Larry` would not be
//     LABELLED "Larry Underwood" -- the book calls him Larry -- but the label is
//     chosen separately, from the most-mentioned member. So the guard was only ever
//     costing a group its INTRODUCTION. Removing it took Le Fleau's top 16 from ~4
//     useful introductions to 11: Nick Andros "prisonnier du sherif de Shoyo", Ralph
//     Brentner "l'homme de la radio", Nadine Cross "douce, gentille avec les enfants,
//     excellente institutrice", Randall Flagg "l'homme noir".
//     ONE RULE WAS ANSWERING TWO QUESTIONS -- which member to show, and which member
//     to quote -- and they have different right answers.
//
//  8. ON A TIE, THE SENTENCE COMES FROM THE FULLER NAME. Stu and Stuart Redman are
//     both tier 3; the tie went alphabetically to Stu, and the group kept a sentence
//     saying nothing while the perfect appositive sat one member away.
//
//  9. PREFIX EDGES ARE WORTH TAKING, GUARDED TWICE. Stu -> Stuart, Fran -> Frannie,
//     Deb -> Deborah, Dex -> Dexter. Guard one: a plural is not a nickname (Noir ->
//     Noirs and Etat -> Etats were 2 of the first 8 merges). Guard two: an ambiguous
//     prefix is declined unless one extension dominates 3:1 -- `Fran` extends to both
//     `Frank` and `Frannie`, and merging Frank into Frannie is far worse than leaving
//     a nickname unlinked. `Carl ~ Carla, Carley Yates` is still declined, correctly.
//
//     MEASURED, because it is the one rule whose value was not obvious. Le Fleau: 7
//     merges, 2 clearly right (Stu and Fran -- ranks 1 and 4, the highest-value
//     entities in the book), 2 clearly wrong (Brad -> Bradenton, Rich -> Richardson),
//     3 unverifiable. Dexter: 2 merges, both right, none wrong. Neuromancien: none
//     drawn. It earns itself because ITS WINS LAND AT THE TOP OF THE LIST AND ITS
//     ERRORS LAND IN THE TAIL, on entities with under 30 mentions that a reader is
//     unlikely to look up -- and where the cost is one imperfect sentence, not a
//     wrong list. `--prefix 0` turns it off; the comparison is the evidence.
//
// 10. A RUNNING HEADER IS NOT A CHARACTER, and the position finds it where the tag
//     does not. SKIPPING `Heading` BLOCKS WAS THE OBVIOUS FIX AND IS A NO-OP -- worth
//     recording, because it is the first thing anyone will try. Two of three real
//     EPUBs contain NO h1-h6 at all (Le Fleau: 0 of 14,662 blocks; Dexter: 0), and the
//     third's 32 of 3,241 changed the list not at all -- identical rows, identical
//     sentences, counts moving by one. Real books put chapter titles in
//     <p class="...">, which document.h correctly calls a Paragraph.
//
//     What works is measuring WHERE a run sits: a running header appears in the FIRST
//     BLOCK of every chapter file. Real characters spend 0-7% of their mentions
//     opening a chapter; the book's own title run is far above that. The cut is 50%,
//     applied per RUN and BEFORE grouping -- the title folds into the character
//     sharing its name and dilutes the group's figure (Dexter's group reads 29% with
//     the title in it and 18% without).
//
//     Measured across the three books: it removes exactly ONE run, `Darkly Dreaming
//     Dexter`, and on the other two it removes NOTHING and leaves their top 40
//     byte-identical. `--frontcut 0` turns it off; the `f` column is the percentage.
//
// 11. THE PRONOUN LEAK WAS NEVER LEXICAL. `Il` and `Elle` ranked 7th and 8th on a
//     short book, and the obvious diagnosis -- that separating a pronoun from a name
//     needs a stopword list, and therefore a per-language dictionary -- was wrong.
//     A pronoun can only rank at all if it appears MID-SENTENCE with a capital, so
//     every one of its mentions was a sentence boundary this file had missed. Dumping
//     the preceding 46 bytes (`--why Il`) showed it in one screen: every case ended in
//     a closing guillemet.
//
//     Two positional causes, no vocabulary:
//
//     a. NBSP IS WHITESPACE FOR SPLITTING. French typography sets a non-breaking space
//        inside guillemets and before ! ? :, so a quoted sentence really reads
//        "\u00ABBonjour\u00A0!\u00A0\u00BB". Skipping only ASCII spaces meant the
//        splitter stopped on the NBSP, never reached the closing guillemet, and never
//        split -- so the NEXT sentence's opening word counted as mid-sentence. Fixing
//        it found 268 more sentences in Neuromancien and 163 in Le Fleau, and REMOVED
//        Il and Elle from Neuromancien's list entirely. `Le Finlandais` lost its
//        spurious `Le` alias at the same time and for the same reason.
//
//     b. A WORD OPENING REPORTED SPEECH IS SENTENCE-INITIAL WHEREVER IT SITS.
//        `Elle disait : "Je n'arrive pas a respirer"` puts `Je` mid-sentence by
//        position and first-word by grammar. Only a quote or a colon counts --
//        deliberately NOT a dash or a paren, since French sets parenthetical
//        em-dashes mid-sentence ("les trois - Stu, Larry et Glen - partirent") and
//        suppressing a real name there could push it under the threshold.
//
//     Le Fleau's `Je` and `Tu` are gone and `Il` fell to rank 57 with 27 mentions.
//     The top twelve moved by 2-4 mentions each and did not reorder, so nothing real
//     was suppressed. THE LESSON IS THE SHAPE: a false positive that looks like it
//     needs a dictionary may be a punctuation bug wearing a linguistic disguise, and
//     the way to tell is to print the context rather than reason about the category.
//
// WHAT IS STILL WRONG, and what 3E inherits:
//
//  * PERSON VERSUS PLACE IS NOT SEPARATED, by decision -- the screen says NAMES.
//    Boulder, Las Vegas, New York and Miami all rank, correctly.
//  * `Il` SURVIVES AT RANK 57 in Le Fleau, 27 mentions, below anything a reader
//    would scroll to. The residue is other punctuation edges; see rule 11 for the two
//    causes that were fixed and why chasing the rest is not worth a rule.
//  * ALIAS LISTS GET LONG -- "Stu (Stuart/Stu Redman/Redman/Stuart Redman)" does not
//    fit a 480px row. A display problem, not a detection one.
//
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/document.h"
#include "reader/host_fs.h"

namespace {

// ---------------------------------------------------------------- character class
// UTF-8, but only as far as Latin-1 -- which is exactly as far as fontc.py's subset
// goes, so it is also as far as anything could be RENDERED. Past that, Other.
enum Cls { Upper, Lower, Apos, Digit, SentEnd, QuoteOpen, Dash, Comma, Space, Other };

struct Ch {
  int len;
  Cls cls;
};

Ch classify(const char* p, const char* end) {
  const unsigned char c0 = static_cast<unsigned char>(*p);
  if (c0 < 0x80) {
    if (c0 >= 'A' && c0 <= 'Z') return {1, Upper};
    if (c0 >= 'a' && c0 <= 'z') return {1, Lower};
    if (c0 >= '0' && c0 <= '9') return {1, Digit};
    if (c0 == '\'') return {1, Apos};
    if (c0 == '.' || c0 == '!' || c0 == '?') return {1, SentEnd};
    if (c0 == '"') return {1, QuoteOpen};
    if (c0 == '-') return {1, Dash};
    if (c0 == ',') return {1, Comma};
    if (c0 == ' ' || c0 == '\t' || c0 == '\n' || c0 == '\r') return {1, Space};
    return {1, Other};
  }
  if (c0 == 0xC3 && p + 1 < end) {
    const unsigned char c1 = static_cast<unsigned char>(p[1]);
    if (c1 == 0x97 || c1 == 0xB7) return {2, Other};   // multiply, divide
    if (c1 >= 0x80 && c1 <= 0x9E) return {2, Upper};   // A-grave .. Thorn
    if (c1 == 0x9F) return {2, Lower};                 // sharp s
    if (c1 >= 0xA0 && c1 <= 0xBF) return {2, Lower};   // a-grave .. y-diaeresis
    return {2, Other};
  }
  if (c0 == 0xC2 && p + 1 < end) {
    const unsigned char c1 = static_cast<unsigned char>(p[1]);
    if (c1 == 0xAB || c1 == 0xBB) return {2, QuoteOpen};  // guillemets
    // NBSP IS WHITESPACE FOR SPLITTING, and missing that was the whole pronoun leak.
    // French typography sets one INSIDE guillemets and before ! ? : -- so a quoted
    // sentence really reads "\u00ABBonjour\u00A0!\u00A0\u00BB". Skipping only ASCII
    // spaces meant the splitter stopped dead on the NBSP, never reached the closing
    // guillemet, and never split -- so the NEXT sentence's opening word counted as
    // mid-sentence. That, and nothing lexical, is why Il and Elle ranked.
    if (c1 == 0xA0) return {2, Space};                    // nbsp
    return {2, Other};
  }
  if (c0 == 0xE2 && p + 2 < end) {
    const unsigned char c1 = static_cast<unsigned char>(p[1]);
    const unsigned char c2 = static_cast<unsigned char>(p[2]);
    if (c1 == 0x80) {
      if (c2 == 0x99 || c2 == 0x98) return {3, Apos};                 // curly apostrophes
      if (c2 == 0x9C || c2 == 0x9D) return {3, QuoteOpen};            // curly quotes
      if (c2 == 0x93 || c2 == 0x94) return {3, Dash};                 // en/em dash
      if (c2 == 0xA6) return {3, SentEnd};                            // ellipsis
      if (c2 == 0xAF || c2 == 0x89 || c2 == 0x8A) return {3, Space};   // narrow/thin spaces
    }
  }
  // Any other lead byte: skip the whole sequence.
  if (c0 >= 0xF0) return {4, Other};
  if (c0 >= 0xE0) return {3, Other};
  if (c0 >= 0xC0) return {2, Other};
  return {1, Other};
}

bool isWordCls(Cls c) { return c == Upper || c == Lower || c == Apos || c == Digit; }


// TRIM WHAT AN APOSTROPHE GLUES ON, at both ends. English possessives and
// contractions made `I'm`, `I'd` and `Deborah's` read as names; French elision made
// `d'Amy` and `l'Enfant` read as `D'Amy` and `L'Enfant`. Both are one rule each and
// neither needs a vocabulary.
//
// A leading 1-2 letter fragment before an apostrophe is elision -> drop it.
// A trailing 1-2 lowercase letters after an apostrophe is a possessive or a
// contraction -> drop it. "O'Brien" survives as "Brien": the O is lost, which is the
// price, and a 5-letter tail is not a contraction.
std::string trimApostrophe(const std::string& t) {
  const char* p = t.data();
  const char* end = p + t.size();
  {
    const char* q = p;
    int n = 0;
    while (q < end && n <= 2) {
      Ch c = classify(q, end);
      if (c.cls == Apos) {
        if (n >= 1) return trimApostrophe(std::string(q + c.len, end));
        break;
      }
      if (!isWordCls(c.cls)) break;
      ++n;
      q += c.len;
    }
  }
  const char* lastA = nullptr;
  int lastLen = 0;
  for (const char* r = p; r < end;) {
    Ch c = classify(r, end);
    if (c.cls == Apos) { lastA = r; lastLen = c.len; }
    r += c.len;
  }
  if (lastA != nullptr) {
    int m = 0;
    bool allLower = true;
    for (const char* r = lastA + lastLen; r < end;) {
      Ch c = classify(r, end);
      if (c.cls != Lower) allLower = false;
      ++m;
      r += c.len;
    }
    if (allLower && m >= 1 && m <= 2) return std::string(p, lastA);
  }
  return t;
}

// ------------------------------------------------------------------- sentences
// Split on . ! ? ellipsis. Guarded against abbreviations: a period straight after a
// short capitalised token (M. Mme Dr St) is not a boundary, which matters a great
// deal in French.
std::vector<std::string_view> sentences(const std::string& text) {
  std::vector<std::string_view> out;
  const char* base = text.data();
  const char* end = base + text.size();
  const char* start = base;
  const char* p = base;
  while (p < end) {
    Ch ch = classify(p, end);
    if (ch.cls != SentEnd) {
      p += ch.len;
      continue;
    }
    // How long is the token immediately before this terminator, and was it capitalised?
    const char* q = p;
    int tokLen = 0;
    bool tokUpper = false;
    while (q > base) {
      // step back one byte at a time; continuation bytes are 0x80..0xBF
      const char* r = q - 1;
      while (r > base && (static_cast<unsigned char>(*r) & 0xC0) == 0x80) --r;
      Ch back = classify(r, end);
      if (!isWordCls(back.cls)) break;
      tokUpper = (back.cls == Upper);
      ++tokLen;
      q = r;
    }
    // consume the run of terminators
    const char* t = p;
    while (t < end) {
      Ch c = classify(t, end);
      if (c.cls != SentEnd) break;
      t += c.len;
    }
    // A SHORT CAPITALISED TOKEN IS NOT AN ABBREVIATION JUST FOR BEING SHORT: names
    // are short. "parla a Stu. Je crois" never split under that rule, which is the
    // whole reason Je / Il / Et scored as mid-sentence words in the first pass.
    // Either a single-letter initial (J. R. R., and M. for Monsieur) or a listed one.
    std::string tokTxt(q, static_cast<size_t>(p - q));
    static const char* kAbbrev[] = {"Mme", "Mlle", "Dr",  "St", "Ste", "Mr",
                                    "Mrs", "Ms",   "Prof", "No", "Nos", "Vol"};
    bool listed = false;
    for (const char* a : kAbbrev) if (tokTxt == a) { listed = true; break; }
    const bool abbrev = (tokLen == 1 && tokUpper) || listed;
    // Skip trailing quotes/brackets, then whitespace.
    // Whitespace and closing marks in EITHER order: `! " La` and `!" La` both occur,
    // and skipping only quotes-then-space missed the first, which left the next
    // sentence's opening word looking mid-sentence.
    const char* u = t;
    bool sawSpace = false;
    while (u < end) {
      Ch c = classify(u, end);
      if (c.cls == Space) { sawSpace = true; u += c.len; continue; }
      if (c.cls == QuoteOpen || (c.len == 1 && (*u == ')' || *u == ']'))) { u += c.len; continue; }
      break;
    }
    const char* v = u;
    if (v >= end) sawSpace = true;
    if (!abbrev && sawSpace) {
      out.push_back(std::string_view(start, static_cast<size_t>(u - start)));
      start = v;
      p = v;
      continue;
    }
    p = t;
  }
  if (start < end) out.push_back(std::string_view(start, static_cast<size_t>(end - start)));
  return out;
}

// ---------------------------------------------------------------------- records
struct Occurrence {
  int spine = 0;
  std::string sentence;
  bool followedByComma = false;
  bool afterCommaLower = false;  // the word after the comma is not itself a name
  bool dialogue = false;
  int names = 0;
};

struct Cand {
  long mentions = 0;
  long initial = 0;
  long frontBlock = 0;  // mentions sitting in a chapter's first two blocks
  int firstSpine = -1;
  Occurrence best;
  int bestTier = -1;
};

// WHAT AN INTRODUCTION LOOKS LIKE, in tiers -- and the EARLIEST occurrence of the
// best available tier wins, not the highest-scoring one anywhere in the book.
//
// The first pass scored a running best over every occurrence and rewarded length,
// which over 900 occurrences selects for the most verbose sentence in the novel: it
// replaced "Harold, un gros garcon de seize ans" with a ramble. Length was a usable
// proxy over six samples and is a trap over nine hundred. An introduction also
// happens ONCE, and early, so "first of the best kind" is both cheaper and truer.
//
// Tier 3 is the appositive -- `Name, <lowercase noun phrase>` -- which is how novels
// introduce people. Requiring the word after the comma to be lowercase is what kills
// the front-matter case for free: "Stu, Larry, Glen, Frannie..." names eight people
// and introduces none, and the word after that comma is a capital.
int tierOf(const Occurrence& o) {
  const size_t len = o.sentence.size();
  if (o.dialogue || o.names > 3) return 0;
  if (o.followedByComma && o.afterCommaLower && len <= 220) return 3;
  if (len >= 50 && len <= 250) return 2;
  return 1;
}

bool dialogueStart(std::string_view s) {
  const char* p = s.data();
  const char* end = p + s.size();
  while (p < end) {
    Ch sp = classify(p, end);
    if (sp.cls != Space) break;
    p += sp.len;
  }
  if (p >= end) return false;
  Ch c = classify(p, end);
  return c.cls == QuoteOpen || c.cls == Dash;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: name_probe <book.epub> [--min N] [--top N]\n");
    return 2;
  }
  long minCount = 3;
  int top = 60;
  bool usePrefix = true;
  bool skipHeadings = true;
  const char* why = nullptr;
  long frontCut = 50;  // drop a run this % or more of whose mentions are chapter-openers
  for (int i = 2; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--min") == 0) minCount = std::atol(argv[i + 1]);
    else if (std::strcmp(argv[i], "--top") == 0) top = std::atoi(argv[i + 1]);
    else if (std::strcmp(argv[i], "--prefix") == 0) usePrefix = (std::atoi(argv[i + 1]) != 0);
    else if (std::strcmp(argv[i], "--headings") == 0) skipHeadings = (std::atoi(argv[i + 1]) == 0);
    else if (std::strcmp(argv[i], "--frontcut") == 0) frontCut = std::atol(argv[i + 1]);
    else if (std::strcmp(argv[i], "--why") == 0) why = argv[i + 1];
  }

  reader::HostFileSystem fs("/");
  reader::OpenedBook book;
  const char* reason = "";
  if (!reader::openBook(fs, argv[1], book, &reason)) {
    std::fprintf(stderr, "openBook failed: %s\n", reason);
    return 1;
  }
  std::printf("book   : %s\nauthor : %s\nspine  : %d entries\n\n", book.title.c_str(),
              book.author.c_str(), book.chapterCount());

  std::map<std::string, Cand> cands;
  long words = 0, blocks = 0, sents = 0, bytes = 0, headings = 0;
  int whyShown = 0;
  // WHAT A NAIVE ON-DEVICE TABLE WOULD COST. The probe holds every candidate for
  // the whole book because a desktop can; the device has a 45,840-byte floor, so
  // this is the number 3E's design turns on. Peak per CHAPTER is the interesting
  // one: it is what a scan that merges into the card per chapter would need.
  size_t peakChapterRuns = 0, peakChapterBytes = 0;
  std::map<std::string, size_t> chapterRuns;

  struct Tok {
    std::string text;
    size_t off = 0;   // byte offset of the token's start within the sentence
    bool opener = false;  // preceded by a quote or colon: reported speech starts here
    bool cand = false;
    bool comma = false;
    bool nextLower = false;  // the following word starts lowercase
  };
  std::vector<Tok> toks;

  reader::ChapterReader cr;
  for (int c = 0; c < book.chapterCount(); ++c) {
    const reader::ChapterLocation loc = book.locate(c);
    if (loc.compressedSize == 0) continue;
    if (!cr.begin(fs, loc)) continue;
    chapterRuns.clear();
    int blockInChapter = -1;
    reader::Block b;
    while (cr.next(b)) {
      ++blocks;
      ++blockInChapter;
      // A HEADING IS THE BOOK TALKING ABOUT ITSELF, not the story. Chapter titles put
      // the book's own title in the list ("Dexter 1 - Darkly Dreaming Dexter"), where
      // it then folds into the character who shares its name.
      if (b.kind == reader::BlockKind::Heading) {
        ++headings;
        if (skipHeadings) continue;
      }
      bytes += static_cast<long>(b.text.size());
      for (std::string_view sv : sentences(b.text)) {
        ++sents;
        const bool dlg = dialogueStart(sv);
        toks.clear();
        const char* p = sv.data();
        const char* end = p + sv.size();
        while (p < end) {
          Ch ch = classify(p, end);
          if (!isWordCls(ch.cls)) { p += ch.len; continue; }
          const char* ws = p;
          const bool startUpper = (ch.cls == Upper);
          bool anyLower = false;
          while (p < end) {
            Ch w = classify(p, end);
            if (!isWordCls(w.cls)) break;
            if (w.cls == Lower) anyLower = true;
            p += w.len;
          }
          ++words;
          Tok t;
          t.off = static_cast<size_t>(ws - sv.data());
          t.text = trimApostrophe(std::string(ws, static_cast<size_t>(p - ws)));
          // Re-decide on the TRIMMED token: "I'm" trims to "I", a single capital,
          // which the Xxxx rule then rejects as an initial.
          bool tUp = false, tLow = false;
          for (const char* r = t.text.data(); r < t.text.data() + t.text.size();) {
            Ch c = classify(r, t.text.data() + t.text.size());
            if (r == t.text.data()) tUp = (c.cls == Upper);
            else if (c.cls == Lower) tLow = true;
            r += c.len;
          }
          (void)startUpper; (void)anyLower;
          t.cand = tUp && tLow;
          if (p < end) t.comma = (classify(p, end).cls == Comma);
          toks.push_back(std::move(t));
        }
        for (size_t i = 0; i + 1 < toks.size(); ++i) toks[i].nextLower = !toks[i + 1].cand;

        // A WORD OPENING REPORTED SPEECH IS SENTENCE-INITIAL WHEREVER IT SITS.
        // `Elle disait : "Je n'arrive pas a respirer"` puts `Je` mid-sentence by
        // position and first-word by grammar, and it was the residue of the pronoun
        // leak after the NBSP fix. Only a quote or a colon counts -- deliberately NOT
        // a dash or a paren, because French sets parenthetical em-dashes mid-sentence
        // ("les trois - Stu, Larry et Glen - partirent") and suppressing a real name
        // there could push it under the threshold. Position, not vocabulary.
        for (Tok& t : toks) {
          const char* r = sv.data() + t.off;
          while (r > sv.data()) {
            const char* q = r - 1;
            while (q > sv.data() && (static_cast<unsigned char>(*q) & 0xC0) == 0x80) --q;
            Ch c = classify(q, sv.data() + sv.size());
            if (c.cls == Space) { r = q; continue; }
            if (c.cls == QuoteOpen || *q == ':') t.opener = true;
            break;
          }
        }

        // AN ENTITY IS A MAXIMAL RUN, COUNTED ONCE. Counting each token separately as
        // well is what put `La` in the list beside `La Poubelle` with 316 of its own
        // mentions -- every "La Poubelle" was also being counted as a "La".
        std::vector<std::pair<size_t, size_t>> spans;
        for (size_t i = 0; i < toks.size();) {
          if (!toks[i].cand) { ++i; continue; }
          size_t j = i;
          while (j + 1 < toks.size() && toks[j + 1].cand && !toks[j].comma) ++j;
          spans.push_back({i, j});
          i = j + 1;
        }
        const int names = static_cast<int>(spans.size());

        for (auto& [i, j] : spans) {
          if (why != nullptr && i != 0 && toks[i].text == why && whyShown < 24) {
            ++whyShown;
            const size_t o = toks[i].off;
            const size_t from = o > 46 ? o - 46 : 0;
            std::string before(sv.data() + from, o - from);
            for (char& ch : before) if (ch == '\n' || ch == '\r') ch = ' ';
            std::printf("  ...%s[%s]\n", before.c_str(), why);
          }
          std::string run = toks[i].text;
          for (size_t k = i + 1; k <= j; ++k) { run += ' '; run += toks[k].text; }
          // name bytes + one stored sentence + ~12 bytes of counters
          if (chapterRuns.find(run) == chapterRuns.end())
            chapterRuns[run] = run.size() + 12;
          Cand& cd = cands[run];
          ++cd.mentions;
          if (i == 0 || toks[i].opener) ++cd.initial;
          if (blockInChapter <= 1) ++cd.frontBlock;
          if (cd.firstSpine < 0) cd.firstSpine = c;
          Occurrence o;
          o.spine = c;
          o.sentence.assign(sv.data(), sv.size());
          o.followedByComma = toks[j].comma;
          o.afterCommaLower = toks[j].nextLower;
          o.dialogue = dlg;
          o.names = names;
          const int t = tierOf(o);
          if (t > cd.bestTier) { cd.bestTier = t; cd.best = std::move(o); }
        }
      }
    }
    size_t cb = 0;
    for (auto& kv : chapterRuns) cb += kv.second;
    if (chapterRuns.size() > peakChapterRuns) peakChapterRuns = chapterRuns.size();
    if (cb > peakChapterBytes) peakChapterBytes = cb;
  }

  std::printf("walked : %ld blocks, %ld sentences, %ld words, %ld text bytes\n", blocks, sents,
              words, bytes);
  std::printf("headings: %ld (%s)\n", headings, skipHeadings ? "skipped" : "included");
  {
    size_t wholeBook = 0;
    for (auto& kv : cands) wholeBook += kv.first.size() + 12;
    std::printf("\nMEMORY, if a table held every candidate:\n");
    std::printf("  whole book : %zu runs, %zu bytes of keys+counters (no sentences)\n",
                cands.size(), wholeBook);
    std::printf("  worst chapter: %zu runs, %zu bytes\n", peakChapterRuns, peakChapterBytes);
    std::printf("  ...against a measured 45,840-byte device heap floor.\n");
  }

  // Is this a list or a dictionary? A NAMES screen has to be scrollable, not endless.
  std::printf("\nentities by mention threshold:\n");
  for (long t : {1L, 2L, 3L, 5L, 10L, 25L, 50L, 100L}) {
    long n = 0;
    for (auto& kv : cands) if (kv.second.mentions - kv.second.initial >= t) ++n;
    std::printf("  >= %-4ld  %ld\n", t, n);
  }
  long onlyInitial = 0;
  for (auto& kv : cands) if (kv.second.mentions == kv.second.initial) ++onlyInitial;
  std::printf("  never seen mid-sentence (suppressed): %ld\n", onlyInitial);

  // ------------------------------------------------------------------ grouping
  // Two kinds of edge join entities that name one person, and neither needs a
  // vocabulary or a list of nicknames.
  //
  //  * CONTAINMENT: a single token that is the head or tail of a longer run, when the
  //    run accounts for most of that token's mentions. "Redman" is always "Stuart
  //    Redman". "Larry" is NOT folded into "Larry Underwood" and should not be -- the
  //    book calls him Larry far more often than it calls him Larry Underwood.
  //
  //  * PREFIX: a token that is a strict prefix of another entity's FIRST token.
  //    Stu -> Stuart Redman, Fran -> Frannie, Deb -> Deborah, Dex -> Dexter. This is
  //    the edge that matters most, because THE INTRODUCTION HAPPENS AT THE FULL NAME
  //    AND THE READER IS STUCK ON THE FORENAME: without it, "Stuart Redman, sans
  //    doute l'homme le plus tranquille d'Arnette" is filed under Redman while `Stu`
  //    -- 849 mentions, the one actually on the page -- gets a sentence saying nothing.
  //
  //    GUARDED ON AMBIGUITY, not just on length. `Fran` is a prefix of `Frannie` AND
  //    of `Frank`, and unioning on both makes two people one person -- a far worse
  //    failure than leaving a nickname unlinked. So a prefix edge is drawn only when
  //    exactly ONE frequent entity extends it. An ambiguous nickname is not resolvable
  //    without semantics, so it is declined and said so.
  auto charLen = [](const std::string& t) {
    int n = 0;
    for (const char* r = t.data(); r < t.data() + t.size();) { r += classify(r, t.data() + t.size()).len; ++n; }
    return n;
  };
  auto firstTok = [](const std::string& e) {
    const size_t sp = e.find(' ');
    return sp == std::string::npos ? e : e.substr(0, sp);
  };

  // Only entities above the threshold take part; a hapax must not drag a name around.
  // A RUNNING HEADER IS NOT A CHARACTER, and it has a sharp signature: it sits in the
  // FIRST BLOCK of every chapter file. Excluding `Heading` blocks was the obvious fix
  // and turned out to be a no-op -- two of three real EPUBs have no h1-h6 at all, and
  // the third's 32 changed nothing, because real books put chapter titles in
  // <p class="...">. This measures the position instead of trusting the tag.
  //
  // Measured: real characters run 0-7% chapter-opening mentions; a book's own title
  // run is far higher. The cut is applied per RUN and BEFORE grouping, because the
  // title folds into the character sharing its name and dilutes the group's figure.
  std::vector<const std::string*> live;
  long cut = 0;
  for (auto& [name, cd] : cands) {
    if (cd.mentions - cd.initial < minCount) continue;
    if (frontCut > 0 && cd.mentions > 0 && cd.frontBlock * 100 / cd.mentions >= frontCut) {
      ++cut;
      continue;
    }
    live.push_back(&name);
  }
  std::printf("\nfurniture cut (>=%ld%% of mentions open a chapter): %ld runs\n", frontCut, cut);

  std::map<std::string, std::string> parent;
  for (const std::string* n : live) parent[*n] = *n;
  std::function<std::string(std::string)> findRoot = [&](std::string x) {
    while (parent[x] != x) x = parent[x];
    return x;
  };
  auto unite = [&](const std::string& a, const std::string& b) {
    const std::string ra = findRoot(a), rb = findRoot(b);
    if (ra != rb) parent[ra] = rb;
  };

  // --- containment edges
  for (const std::string* np : live) {
    const std::string& name = *np;
    if (name.find(' ') != std::string::npos) continue;
    const std::string* best = nullptr;
    long bestN = 0;
    for (const std::string* op : live) {
      const std::string& other = *op;
      if (other.find(' ') == std::string::npos) continue;
      const bool head = other.compare(0, name.size(), name) == 0 && other[name.size()] == ' ';
      const bool tail = other.size() > name.size() + 1 &&
                        other.compare(other.size() - name.size(), name.size(), name) == 0 &&
                        other[other.size() - name.size() - 1] == ' ';
      if (!head && !tail) continue;
      const long on = cands[other].mentions - cands[other].initial;
      if (on > bestN) { bestN = on; best = &other; }
    }
    // NO RATIO GUARD, and the reason is that the guard was answering two questions
    // with one rule. It existed so that `Larry` would not be LABELLED "Larry
    // Underwood" -- the book calls him Larry. But the label is chosen separately,
    // from the most-mentioned member, so the guard was only ever costing the group
    // its introduction: `Frannie` refused to link to `Frannie Goldsmith` and kept a
    // sentence saying nothing while the appositive sat one member away.
    //
    // Linking is safe here because only the BEST run per token is joined, never every
    // run containing it -- so `Goldsmith` joins one Goldsmith and a family is not
    // collapsed into one person.
    if (best != nullptr) unite(name, *best);
  }

  // --- prefix edges, with the ambiguity guard
  struct Merge { std::string from, to; };
  std::vector<Merge> merges;
  std::vector<std::pair<std::string, std::vector<std::string>>> declined;
  for (const std::string* np : live) {
    if (!usePrefix) break;
    const std::string& name = *np;
    if (name.find(' ') != std::string::npos) continue;
    if (charLen(name) < 3) continue;
    std::vector<std::string> ext;
    for (const std::string* op : live) {
      const std::string& other = *op;
      if (other == name) continue;
      const std::string ft = firstTok(other);
      if (ft.size() <= name.size()) continue;
      if (ft.compare(0, name.size(), name) != 0) continue;
      // A PLURAL IS NOT A NICKNAME. Noir -> Noirs and Etat -> Etats were two of the
      // eight merges this rule first drew, and both are one word inflected.
      const std::string tail = ft.substr(name.size());
      if (tail == "s" || tail == "es" || tail == "x") continue;
      // Distinct only if the extending FIRST TOKEN differs -- "Stuart" and
      // "Stuart Redman" are not two ways to be ambiguous.
      bool seen = false;
      for (const std::string& e : ext) if (firstTok(e) == ft) seen = true;
      if (!seen) ext.push_back(other);
    }
    if (ext.empty()) continue;
    // AMBIGUOUS, BUT NOT ALWAYS UNRESOLVABLE. `Fran` extends to both `Frank` and
    // `Frannie`, and declining outright cost a top-three entity its introduction.
    // A DOMINANT extension -- 3x the mentions of the runner-up -- is taken; a close
    // call is still declined, because merging Frank into Frannie is far worse than
    // leaving a nickname unlinked.
    std::sort(ext.begin(), ext.end(), [&](const std::string& a, const std::string& b) {
      return cands[a].mentions - cands[a].initial > cands[b].mentions - cands[b].initial;
    });
    if (ext.size() > 1) {
      const long n0 = cands[ext[0]].mentions - cands[ext[0]].initial;
      const long n1 = cands[ext[1]].mentions - cands[ext[1]].initial;
      if (n0 < n1 * 3) { declined.push_back({name, ext}); continue; }
    }
    unite(name, ext[0]);
    merges.push_back({name, ext[0]});
  }

  struct Ent {
    long n = 0;
    const Cand* src = nullptr;
    std::string srcName;
    std::vector<std::string> members;
  };
  std::map<std::string, Ent> groups;
  for (const std::string* np : live) {
    const Cand& cd = cands[*np];
    Ent& g = groups[findRoot(*np)];
    g.n += cd.mentions - cd.initial;
    g.members.push_back(*np);
    // The sentence comes from the best tier, and ON A TIE FROM THE FULLER NAME --
    // which is the whole point of grouping, and which the first version got wrong.
    // Stu and Stuart Redman are both tier 3, the tie went alphabetically to Stu, and
    // the group kept a sentence saying nothing while the perfect appositive sat one
    // member away. More tokens first, then longer.
    const auto fuller = [&](const std::string& a, const std::string& b) {
      const long ta = std::count(a.begin(), a.end(), ' ');
      const long tb = std::count(b.begin(), b.end(), ' ');
      if (ta != tb) return ta > tb;
      return a.size() > b.size();
    };
    if (g.src == nullptr || cd.bestTier > g.src->bestTier ||
        (cd.bestTier == g.src->bestTier && fuller(*np, g.srcName))) {
      g.src = &cd;
      g.srcName = *np;
    }
  }

  std::vector<std::pair<std::string, Ent*>> ranked;
  for (auto& kv : groups) ranked.push_back({kv.first, &kv.second});
  std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) {
    if (a.second->n != b.second->n) return a.second->n > b.second->n;
    return a.first < b.first;
  });

  std::printf("\nprefix rule: %s\n", usePrefix ? "ON" : "OFF");
  std::printf("prefix merges drawn: %zu\n", merges.size());
  for (auto& m : merges) std::printf("    %s -> %s\n", m.from.c_str(), m.to.c_str());
  std::printf("prefix merges DECLINED as ambiguous: %zu\n", declined.size());
  for (auto& [n, ex] : declined) {
    std::printf("    %s ~ ", n.c_str());
    for (size_t i = 0; i < ex.size(); ++i) { if (i) std::printf(", "); std::printf("%s", ex[i].c_str()); }
    std::printf("\n");
  }

  std::printf("\nentities: %zu at min %ld (of %zu distinct runs)\n\n", ranked.size(), minCount,
              cands.size());
  int shown = 0;
  for (auto& [root, g] : ranked) {
    if (shown++ >= top) break;
    // Label: the most-mentioned member first, so what is ON THE PAGE is findable,
    // then the rest -- the full name is what actually answers "who is this".
    std::sort(g->members.begin(), g->members.end(), [&](const std::string& a, const std::string& b) {
      return cands[a].mentions - cands[a].initial > cands[b].mentions - cands[b].initial;
    });
    std::string label = g->members[0];
    if (g->members.size() > 1) {
      label += " (";
      for (size_t i = 1; i < g->members.size(); ++i) { if (i > 1) label += "/"; label += g->members[i]; }
      label += ")";
    }
    std::string snip = g->src ? g->src->best.sentence : std::string();
    if (snip.size() > 150) { snip.resize(150); snip += "..."; }
    for (char& ch : snip) if (ch == '\n' || ch == '\r') ch = ' ';
    long front = 0, tot = 0;
    for (const std::string& m : g->members) { front += cands[m].frontBlock; tot += cands[m].mentions; }
    std::printf("%5ld T%d f%-3ld %-34s %s\n", g->n, g->src ? g->src->bestTier : -1,
                tot ? front * 100 / tot : 0, label.c_str(), snip.c_str());
  }
  return 0;
}
