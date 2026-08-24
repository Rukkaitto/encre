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
// dog -- and the one false positive is `Dieu`, an interjection. Neuromancien returns
// Case, Molly, Armitage, Wintermute, Chiba, Tessier-Ashpool.
//
// FIVE RULES, each of which replaced something that looked reasonable and was wrong:
//
//  1. SENTENCE-INITIAL SUPPRESSION IS THE LOAD-BEARING RULE. Rank by mentions that
//     are NOT the first word of a sentence. Ranking by total mentions floods the list
//     with Il / Je / Et / Elle -- a French pronoun starts thousands of sentences.
//     This one rule is the difference between a cast list and a word frequency table.
//
//  2. A SHORT CAPITALISED WORD BEFORE A PERIOD IS NOT AN ABBREVIATION. Names are
//     short. Guarding on length alone means "parla a Stu. Je crois" never splits, and
//     every following word counts as mid-sentence -- which is how the pronouns got in.
//     Only a single-letter initial or a listed abbreviation.
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
// WHAT IT DID NOT SOLVE, and what 3E has to decide:
//
//  * THE INTRODUCTION HAPPENS AT THE FULL NAME AND THE READER IS STUCK ON THE
//    FORENAME. "Stuart Redman, sans doute l'homme le plus tranquille d'Arnette" is a
//    perfect answer filed under `Redman`; `Stu`, which is what is on the page 849
//    times, gets a sentence that says nothing. Surnames fold into full names by
//    string containment; Stu -> Stuart does not, because they are different tokens.
//    A prefix rule (Stu c Stuart, Fran c Frannie, Tom c Thomas) is untested and is
//    the obvious next thing to try.
//  * PERSON VERSUS PLACE IS NOT SEPARATED and by decision is not attempted -- the
//    screen says NAMES. Boulder, Las Vegas, New York and Miami all rank, correctly.
//  * `Dieu` -- an interjection that is capitalised, frequent and mid-sentence. No
//    non-semantic rule reaches it.
//
#include <algorithm>
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
enum Cls { Upper, Lower, Apos, Digit, SentEnd, QuoteOpen, Dash, Comma, Other };

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
    if (c1 == 0xA0) return {2, Other};                    // nbsp
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
      if (*u == ' ' || *u == '\t' || *u == '\n' || *u == '\r') { sawSpace = true; ++u; continue; }
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
  while (p < end && (*p == ' ' || *p == '\t')) ++p;
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
  for (int i = 2; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--min") == 0) minCount = std::atol(argv[i + 1]);
    else if (std::strcmp(argv[i], "--top") == 0) top = std::atoi(argv[i + 1]);
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
  long words = 0, blocks = 0, sents = 0, bytes = 0;

  struct Tok {
    std::string text;
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
    reader::Block b;
    while (cr.next(b)) {
      ++blocks;
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
          std::string run = toks[i].text;
          for (size_t k = i + 1; k <= j; ++k) { run += ' '; run += toks[k].text; }
          Cand& cd = cands[run];
          ++cd.mentions;
          if (i == 0) ++cd.initial;
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
  }

  std::printf("walked : %ld blocks, %ld sentences, %ld words, %ld text bytes\n", blocks, sents,
              words, bytes);

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

  // A SINGLE-TOKEN ENTITY FOLDS INTO A LONGER ONE that accounts for most of it:
  // "Redman" is always "Stuart Redman", so it is not a second person. "Larry" is not
  // folded into "Larry Underwood", and should not be -- the book calls him Larry.
  std::map<std::string, std::string> canon;
  for (auto& [name, cd] : cands) {
    canon[name] = name;
    if (name.find(' ') != std::string::npos) continue;
    const std::string* bestRun = nullptr;
    long bestN = 0;
    for (auto& [other, ocd] : cands) {
      if (other.find(' ') == std::string::npos) continue;
      const bool head = other.compare(0, name.size(), name) == 0 && other[name.size()] == ' ';
      const bool tail = other.size() > name.size() + 1 &&
                        other.compare(other.size() - name.size(), name.size(), name) == 0 &&
                        other[other.size() - name.size() - 1] == ' ';
      if (!head && !tail) continue;
      const long on = ocd.mentions - ocd.initial;
      if (on > bestN) { bestN = on; bestRun = &other; }
    }
    if (bestRun != nullptr && bestN * 2 >= cd.mentions - cd.initial) canon[name] = *bestRun;
  }

  struct Ent { long n = 0; const Cand* src = nullptr; std::vector<std::string> alias; };
  std::map<std::string, Ent> ents;
  for (auto& [name, cd] : cands) {
    const long nonInitial = cd.mentions - cd.initial;
    if (nonInitial < minCount) continue;
    Ent& e = ents[canon[name]];
    e.n += nonInitial;
    if (canon[name] != name) e.alias.push_back(name);
    if (e.src == nullptr || nonInitial > e.src->mentions - e.src->initial ||
        cd.bestTier > e.src->bestTier) e.src = &cd;
  }

  std::vector<std::pair<std::string, const Ent*>> ranked;
  for (auto& kv : ents) ranked.push_back({kv.first, &kv.second});
  std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) {
    if (a.second->n != b.second->n) return a.second->n > b.second->n;
    return a.first < b.first;
  });

  std::printf("\nentities: %zu at min %ld (of %zu distinct runs)\n\n", ranked.size(), minCount,
              cands.size());
  int shown = 0;
  for (auto& [name, e] : ranked) {
    if (shown++ >= top) break;
    std::string label = name;
    if (!e->alias.empty()) {
      label += " (";
      for (size_t i = 0; i < e->alias.size(); ++i) { if (i) label += "/"; label += e->alias[i]; }
      label += ")";
    }
    std::string snip = e->src ? e->src->best.sentence : std::string();
    if (snip.size() > 165) { snip.resize(165); snip += "..."; }
    for (char& ch : snip) if (ch == '\n' || ch == '\r') ch = ' ';
    std::printf("%5ld  T%d %-30s sp%-3d %s\n", e->n, e->src ? e->src->bestTier : -1,
                label.c_str(), e->src ? e->src->firstSpine : -1, snip.c_str());
  }
  return 0;
}
