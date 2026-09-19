#include "reader/names.h"

#include "reader/heapguard.h"
#include "reader/name_store.h"

#include <algorithm>
#include <cstring>

namespace reader {
namespace {

// --- Character class -----------------------------------------------------------
//
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
    // NOT LETTERS, and the pair is easy to sweep up by accident: U+00D7 multiply and
    // U+00F7 divide sit inside the accented-letter ranges on both sides.
    if (c1 == 0x97 || c1 == 0xB7) return {2, Other};
    if (c1 >= 0x80 && c1 <= 0x9E) return {2, Upper};  // A-grave .. Thorn
    if (c1 == 0x9F) return {2, Lower};                // sharp s
    if (c1 >= 0xA0 && c1 <= 0xBF) return {2, Lower};  // a-grave .. y-diaeresis
    return {2, Other};
  }
  if (c0 == 0xC2 && p + 1 < end) {
    const unsigned char c1 = static_cast<unsigned char>(p[1]);
    if (c1 == 0xAB || c1 == 0xBB) return {2, QuoteOpen};  // guillemets
    // NBSP IS WHITESPACE FOR SPLITTING, and missing that was the whole pronoun leak.
    // French typography sets one INSIDE guillemets and before ! ? : -- so a quoted
    // sentence really reads "«Bonjour ! »". Skipping only ASCII
    // spaces meant the splitter stopped dead on the NBSP, never reached the closing
    // guillemet, and never split, so the NEXT sentence's opening word counted as
    // mid-sentence. Fixing it found 268 more sentences in Neuromancien and 163 in
    // Le Fleau, and removed Il and Elle from Neuromancien's list entirely.
    if (c1 == 0xA0) return {2, Space};
    return {2, Other};
  }
  if (c0 == 0xE2 && p + 2 < end) {
    const unsigned char c1 = static_cast<unsigned char>(p[1]);
    const unsigned char c2 = static_cast<unsigned char>(p[2]);
    if (c1 == 0x80) {
      if (c2 == 0x99 || c2 == 0x98) return {3, Apos};                // curly apostrophes
      if (c2 == 0x9C || c2 == 0x9D) return {3, QuoteOpen};           // curly quotes
      if (c2 == 0x93 || c2 == 0x94) return {3, Dash};                // en/em dash
      if (c2 == 0xA6) return {3, SentEnd};                           // ellipsis
      if (c2 == 0xAF || c2 == 0x89 || c2 == 0x8A) return {3, Space};  // narrow/thin
    }
  }
  // Any other lead byte: skip the whole sequence rather than one byte, so a
  // multi-byte glyph cannot be read as several Others and split a token.
  if (c0 >= 0xF0) return {4, Other};
  if (c0 >= 0xE0) return {3, Other};
  if (c0 >= 0xC0) return {2, Other};
  return {1, Other};
}

bool isWordCls(Cls c) { return c == Upper || c == Lower || c == Apos || c == Digit; }

// Step back one UTF-8 character from `q`, not below `base`.
const char* prevChar(const char* base, const char* q) {
  const char* r = q - 1;
  while (r > base && (static_cast<unsigned char>(*r) & 0xC0) == 0x80) --r;
  return r;
}

struct Tok {
  std::string text;
  size_t off = 0;        // byte offset into the sentence
  bool cand = false;     // Xxxx: an initial capital and at least one lower after it
  bool comma = false;    // a comma immediately follows
  bool opener = false;   // opens reported speech, so it is sentence-initial by grammar
};

}  // namespace

namespace names_detail {

std::string trimApostrophe(std::string_view token) {
  const char* p = token.data();
  const char* end = p + token.size();
  // A leading 1-2 letter fragment before an apostrophe is elision: drop it.
  {
    const char* q = p;
    int n = 0;
    while (q < end && n <= 2) {
      const Ch c = classify(q, end);
      if (c.cls == Apos) {
        if (n >= 1) return trimApostrophe(std::string_view(q + c.len, static_cast<size_t>(end - q - c.len)));
        break;
      }
      if (!isWordCls(c.cls)) break;
      ++n;
      q += c.len;
    }
  }
  // A trailing 1-2 LOWERCASE letters after an apostrophe is a possessive or a
  // contraction: drop it. A five-letter tail is not a contraction.
  const char* lastA = nullptr;
  int lastLen = 0;
  for (const char* r = p; r < end;) {
    const Ch c = classify(r, end);
    if (c.cls == Apos) {
      lastA = r;
      lastLen = c.len;
    }
    r += c.len;
  }
  if (lastA != nullptr) {
    int m = 0;
    bool allLower = true;
    for (const char* r = lastA + lastLen; r < end;) {
      const Ch c = classify(r, end);
      if (c.cls != Lower) allLower = false;
      ++m;
      r += c.len;
    }
    if (allLower && m >= 1 && m <= 2) return std::string(p, static_cast<size_t>(lastA - p));
  }
  return std::string(token);
}

std::vector<std::string_view> sentences(std::string_view text) {
  std::vector<std::string_view> out;
  const char* base = text.data();
  const char* end = base + text.size();
  const char* start = base;
  const char* p = base;
  while (p < end) {
    const Ch ch = classify(p, end);
    if (ch.cls != SentEnd) {
      p += ch.len;
      continue;
    }
    // How long is the token immediately before this terminator, and was it capitalised?
    const char* q = p;
    int tokLen = 0;
    bool tokUpper = false;
    while (q > base) {
      const char* r = prevChar(base, q);
      const Ch back = classify(r, end);
      if (!isWordCls(back.cls)) break;
      tokUpper = (back.cls == Upper);
      ++tokLen;
      q = r;
    }
    // Consume the run of terminators, so `...` and `?!` are one boundary.
    const char* t = p;
    while (t < end) {
      const Ch c = classify(t, end);
      if (c.cls != SentEnd) break;
      t += c.len;
    }
    const std::string_view tokTxt(q, static_cast<size_t>(p - q));
    static const char* const kAbbrev[] = {"Mme", "Mlle", "Dr",   "St", "Ste", "Mr",
                                          "Mrs", "Ms",   "Prof", "No", "Nos", "Vol"};
    bool listed = false;
    for (const char* a : kAbbrev) {
      if (tokTxt == a) {
        listed = true;
        break;
      }
    }
    const bool abbrev = (tokLen == 1 && tokUpper) || listed;
    // Skip closing marks and whitespace in EITHER order: `! " La` and `!" La` both
    // occur, and skipping only quotes-then-space missed the first, which left the
    // next sentence's opening word looking mid-sentence.
    const char* u = t;
    bool sawSpace = false;
    while (u < end) {
      const Ch c = classify(u, end);
      if (c.cls == Space) {
        sawSpace = true;
        u += c.len;
        continue;
      }
      if (c.cls == QuoteOpen || (c.len == 1 && (*u == ')' || *u == ']'))) {
        u += c.len;
        continue;
      }
      break;
    }
    if (u >= end) sawSpace = true;
    if (!abbrev && sawSpace) {
      out.push_back(std::string_view(start, static_cast<size_t>(u - start)));
      start = u;
      p = u;
      continue;
    }
    p = t;
  }
  if (start < end) out.push_back(std::string_view(start, static_cast<size_t>(end - start)));
  return out;
}

bool dialogueStart(std::string_view s) {
  const char* p = s.data();
  const char* end = p + s.size();
  while (p < end) {
    const Ch c = classify(p, end);
    if (c.cls == Space) {
      p += c.len;
      continue;
    }
    return c.cls == QuoteOpen || c.cls == Dash;
  }
  return false;
}

}  // namespace names_detail

// --- The scanner ---------------------------------------------------------------

void NameScanner::reset() {
  runs_.clear();
  dropped_ = 0;
}

NameScanner::Run* NameScanner::find(std::string_view text) {
  const auto it = std::lower_bound(runs_.begin(), runs_.end(), text,
                                   [](const Run& r, std::string_view t) { return r.text < t; });
  if (it != runs_.end() && it->text == text) return &*it;
  return nullptr;
}

NameScanner::Run* NameScanner::findOrAdd(std::string_view text) {
  // SORTED AND BINARY-SEARCHED, not a map and not a linear scan. `core/` carries
  // exactly one string-keyed map and it is bounded for a reason; and a chapter of a
  // real novel is ~4,000 occurrences over ~400 distinct runs, where a linear scan is
  // ~800,000 string compares and this is ~36,000. The insert's memmove is cheap
  // beside that and happens at most kMaxRuns times.
  const auto it = std::lower_bound(runs_.begin(), runs_.end(), text,
                                   [](const Run& r, std::string_view t) { return r.text < t; });
  if (it != runs_.end() && it->text == text) return &*it;
  if (static_cast<int>(runs_.size()) >= kMaxRuns) {
    ++dropped_;
    return nullptr;
  }
  // ASK THE ALLOCATOR BEFORE GROWING, BECAUSE A VECTOR THAT CANNOT GROW IS abort().
  //
  // REPORTED OFF GLASS as a reboot to Home mid-book, and the dump named this line:
  // operator new threw inside _M_realloc_insert and `-fno-exceptions` turned it into
  // a terminate. The device had 41,616 bytes free and a largest BLOCK of 14,324 --
  // so the free heap said yes and the only number that decides an allocation said
  // no. A doubling realloc holds the old buffer AND the new one, which is the state
  // this probe is documented to be asked in.
  //
  // REFUSING IS A REAL ANSWER HERE: the table is already bounded and already reports
  // what it dropped, so a chapter scanned on a fragmented heap keeps fewer names and
  // says so, where the alternative is losing the reader's page.
  //
  // AN INDEX, NOT THE ITERATOR, ACROSS THE PROBE. `ensureRoom` reserves, a reserve
  // reallocates, and a reallocation invalidates every iterator into the vector --
  // so inserting at `it` afterwards writes through a dangling pointer. It did: the
  // whole unit binary died with SIGBUS and no output, which is what a dangling
  // insert looks like when it happens during static test registration's first run.
  const size_t at = static_cast<size_t>(it - runs_.begin());
  if (!ensureRoom(runs_, runs_.size() + 1)) {
    ++dropped_;
    return nullptr;
  }
  Run r;
  r.text.assign(text);
  return &*runs_.insert(runs_.begin() + static_cast<long>(at), std::move(r));
}

void NameScanner::addBlock(const Block& block, int blockInChapter, RunSink* sink) {
  // A HEADING IS NOT SKIPPED, and that is worth stating because skipping one is the
  // obvious fix and is a no-op. Two of three real EPUBs contain no h1-h6 at all and
  // the third's 32 of 3,241 changed the list not at all: real books put chapter
  // titles in <p class="...">, which document.h correctly calls a Paragraph. What
  // catches a running header is `chapterOpening` below, which is positional.
  std::vector<Tok> toks;
  for (const std::string_view sv : names_detail::sentences(block.text)) {
    const bool dlg = names_detail::dialogueStart(sv);
    toks.clear();
    const char* p = sv.data();
    const char* end = p + sv.size();
    while (p < end) {
      const Ch ch = classify(p, end);
      if (!isWordCls(ch.cls)) {
        p += ch.len;
        continue;
      }
      const char* ws = p;
      while (p < end) {
        const Ch w = classify(p, end);
        if (!isWordCls(w.cls)) break;
        p += w.len;
      }
      Tok t;
      t.off = static_cast<size_t>(ws - sv.data());
      t.text = names_detail::trimApostrophe(
          std::string_view(ws, static_cast<size_t>(p - ws)));
      // RE-DECIDED ON THE TRIMMED TOKEN: "I'm" trims to "I", a single capital, which
      // the Xxxx rule then rejects as an initial rather than admitting as a name.
      bool tUp = false, tLow = false;
      {
        const char* b = t.text.data();
        const char* e = b + t.text.size();
        for (const char* r = b; r < e;) {
          const Ch c = classify(r, e);
          if (r == b) tUp = (c.cls == Upper);
          else if (c.cls == Lower) tLow = true;
          r += c.len;
        }
      }
      t.cand = tUp && tLow;
      if (p < end) t.comma = (classify(p, end).cls == Comma);
      toks.push_back(std::move(t));
    }

    // A WORD OPENING REPORTED SPEECH IS SENTENCE-INITIAL WHEREVER IT SITS.
    // `Elle disait : "Je n'arrive pas a respirer"` puts `Je` mid-sentence by position
    // and first-word by grammar. Only a quote or a colon counts -- deliberately NOT a
    // dash or a paren, since French sets parenthetical em-dashes mid-sentence ("les
    // trois - Stu, Larry et Glen - partirent") and suppressing a real name there could
    // push it under the threshold. Position, not vocabulary.
    for (Tok& t : toks) {
      const char* r = sv.data() + t.off;
      while (r > sv.data()) {
        const char* q = prevChar(sv.data(), r);
        const Ch c = classify(q, sv.data() + sv.size());
        if (c.cls == Space) {
          r = q;
          continue;
        }
        if (c.cls == QuoteOpen || *q == ':') t.opener = true;
        break;
      }
    }

    // AN ENTITY IS A MAXIMAL RUN, COUNTED ONCE. Counting each token separately as
    // well put `La` in the list beside `La Poubelle` with 316 mentions of its own,
    // because every "La Poubelle" was also being counted as a "La".
    for (size_t i = 0; i < toks.size();) {
      if (!toks[i].cand) {
        ++i;
        continue;
      }
      size_t j = i;
      while (j + 1 < toks.size() && toks[j + 1].cand && !toks[j].comma) ++j;

      std::string run = toks[i].text;
      for (size_t k = i + 1; k <= j; ++k) {
        run += ' ';
        run += toks[k].text;
      }
      if (run.size() <= kMaxRunBytes) {
        // SENTENCE-INITIAL SUPPRESSION, the load-bearing rule. `i != 0` is position
        // and `!opener` is grammar; a mention needs both to count.
        const bool mid = (i != 0 && !toks[i].opener);
        Run* r = sinkOnly_ ? nullptr : findOrAdd(run);
        if (r != nullptr) {
          ++r->total;
          if (mid) ++r->midSentence;
          if (blockInChapter <= 1) ++r->chapterOpening;
        }
        // THE SINK SEES THE OCCURRENCE WHETHER OR NOT THE TABLE COULD HOLD IT. A
        // chapter that overflowed kMaxRuns still has extracts worth capturing for
        // the runs it did admit, and the sink is filtering by name anyway.
        if (sink != nullptr) sink->onRun(run, blockInChapter, sv, toks[i].off, mid);
      }
      i = j + 1;
    }
    (void)dlg;
  }
}

std::vector<const NameScanner::Run*> NameScanner::admitted(int minMidSentence) const {
  std::vector<const Run*> out;
  for (const Run& r : runs_) {
    if (r.midSentence < minMidSentence) continue;
    out.push_back(&r);
  }
  // THE FURNITURE CUT IS NOT APPLIED HERE, AND APPLYING IT HERE WAS A REAL BUG --
  // caught by measuring this file against the probe on a real novel rather than by
  // reading it. The cut asks whether a run spends half its mentions OPENING a
  // chapter, and over a whole book that is the book's own running header: measured
  // across three novels it removes exactly one run. Asked per CHAPTER it is a
  // different and much blunter question -- any run that happens to appear only in a
  // chapter's first two blocks scores 100% there -- and it threw away 30 of `Le
  // Fleau`'s 738 admitted runs.
  //
  // So the counts go on the card and the cut is applied to the ACCUMULATED figures,
  // which is why the index stores `chapterOpening` as a field rather than a verdict.
  return out;
}

// --- Grouping ------------------------------------------------------------------

namespace {

size_t tokenCount(std::string_view s) {
  size_t n = 1;
  for (const char c : s) {
    if (c == ' ') ++n;
  }
  return n;
}

std::string_view firstToken(std::string_view s) {
  const size_t sp = s.find(' ');
  return sp == std::string_view::npos ? s : s.substr(0, sp);
}

// Characters, not bytes: a three-letter accented nickname is three characters and
// five bytes, and the prefix rule's floor is about how much of a name you have seen.
size_t charLen(std::string_view s) {
  size_t n = 0;
  for (const char c : s) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
  }
  return n;
}

int rootOf(std::vector<int>& parent, int x) {
  while (parent[static_cast<size_t>(x)] != x) x = parent[static_cast<size_t>(x)];
  return x;
}

void unite(std::vector<int>& parent, int a, int b) {
  const int ra = rootOf(parent, a), rb = rootOf(parent, b);
  if (ra != rb) parent[static_cast<size_t>(ra)] = rb;
}

}  // namespace

std::vector<NameGroup> groupNames(const std::vector<NameIndexEntry>& entries,
                                  int minMidSentence, int furnitureCutPercent) {
  // THE FURNITURE CUT, per RUN and BEFORE grouping. A run that spends at least half
  // its mentions opening a chapter is a running header: measured across three real
  // novels it removes exactly ONE run, the book's own title, and leaves the other
  // two books' top 40 byte-identical. Applied after grouping it would be diluted by
  // the character sharing the title's name.
  std::vector<const NameIndexEntry*> live;
  for (const NameIndexEntry& e : entries) {
    if (e.midSentence < minMidSentence) continue;
    if (furnitureCutPercent > 0 && e.total > 0 &&
        e.chapterOpening * 100 / e.total >= furnitureCutPercent)
      continue;
    live.push_back(&e);
  }
  const int n = static_cast<int>(live.size());
  std::vector<int> parent(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) parent[static_cast<size_t>(i)] = i;

  // --- Containment edges (the probe's rule 7) ---
  //
  // NO RATIO GUARD, AND THAT WAS THE SINGLE LARGEST IMPROVEMENT IN THE PROBE. The
  // guard existed so `Larry` would not be LABELLED "Larry Underwood" -- the book
  // calls him Larry -- but the label is chosen separately, from the most-mentioned
  // member, so the guard was only ever costing a group its other members.
  //
  // Linking is safe because only the BEST run per token is joined, never every run
  // containing it: `Goldsmith` joins one Goldsmith and a family is not collapsed
  // into one person.
  for (int i = 0; i < n; ++i) {
    const std::string& name = live[static_cast<size_t>(i)]->text;
    if (name.find(' ') != std::string::npos) continue;
    int best = -1, bestN = 0;
    for (int j = 0; j < n; ++j) {
      if (i == j) continue;
      const std::string& other = live[static_cast<size_t>(j)]->text;
      if (other.find(' ') == std::string::npos) continue;
      const bool head =
          other.size() > name.size() && other.compare(0, name.size(), name) == 0 &&
          other[name.size()] == ' ';
      const bool tail =
          other.size() > name.size() + 1 &&
          other.compare(other.size() - name.size(), name.size(), name) == 0 &&
          other[other.size() - name.size() - 1] == ' ';
      if (!head && !tail) continue;
      const int on = live[static_cast<size_t>(j)]->midSentence;
      if (on > bestN) {
        bestN = on;
        best = j;
      }
    }
    if (best >= 0) unite(parent, i, best);
  }

  // --- Prefix edges, guarded twice (the probe's rule 9) ---
  //
  // Stu -> Stuart, Fran -> Frannie, Deb -> Deborah, Dex -> Dexter. MEASURED, because
  // it is the one rule whose value was not obvious: on Le Fleau it draws 7 merges, 2
  // clearly right at ranks 1 and 4, 2 clearly wrong in the tail, 3 unverifiable. It
  // earns itself because its WINS LAND AT THE TOP OF THE LIST and its errors land on
  // entities with under 30 mentions that nobody looks up.
  for (int i = 0; i < n; ++i) {
    const std::string& name = live[static_cast<size_t>(i)]->text;
    if (name.find(' ') != std::string::npos) continue;
    if (charLen(name) < 3) continue;
    std::vector<int> ext;
    for (int j = 0; j < n; ++j) {
      if (i == j) continue;
      const std::string& other = live[static_cast<size_t>(j)]->text;
      const std::string_view ft = firstToken(other);
      if (ft.size() <= name.size()) continue;
      if (ft.compare(0, name.size(), name) != 0) continue;
      // GUARD ONE: A PLURAL IS NOT A NICKNAME. `Noir -> Noirs` and `Etat -> Etats`
      // were two of the first eight merges this rule drew, and both are one word
      // inflected rather than two names for one person.
      const std::string_view suffix = ft.substr(name.size());
      if (suffix == "s" || suffix == "es" || suffix == "x") continue;
      // Distinct only if the extending FIRST TOKEN differs: `Stuart` and `Stuart
      // Redman` are not two ways to be ambiguous.
      bool seen = false;
      for (const int k : ext) {
        if (firstToken(live[static_cast<size_t>(k)]->text) == ft) seen = true;
      }
      if (!seen) ext.push_back(j);
    }
    if (ext.empty()) continue;
    std::sort(ext.begin(), ext.end(), [&](int a, int b) {
      return live[static_cast<size_t>(a)]->midSentence > live[static_cast<size_t>(b)]->midSentence;
    });
    // GUARD TWO: AN AMBIGUOUS PREFIX IS DECLINED UNLESS ONE EXTENSION DOMINATES 3:1.
    // `Fran` extends to both `Frank` and `Frannie`, and merging Frank into Frannie is
    // far worse than leaving a nickname unlinked. Declining OUTRIGHT was the first
    // version and it cost a top-three entity its link, so a dominant extension is
    // taken and a close call is still refused.
    if (ext.size() > 1) {
      const int n0 = live[static_cast<size_t>(ext[0])]->midSentence;
      const int n1 = live[static_cast<size_t>(ext[1])]->midSentence;
      if (n0 < n1 * 3) continue;
    }
    unite(parent, i, ext[0]);
  }

  // --- Collect ---
  std::vector<NameGroup> out;
  std::vector<int> groupOf(static_cast<size_t>(n), -1);
  for (int i = 0; i < n; ++i) {
    const int r = rootOf(parent, i);
    if (groupOf[static_cast<size_t>(r)] < 0) {
      groupOf[static_cast<size_t>(r)] = static_cast<int>(out.size());
      out.push_back(NameGroup{});
    }
    NameGroup& g = out[static_cast<size_t>(groupOf[static_cast<size_t>(r)])];
    g.midSentence += live[static_cast<size_t>(i)]->midSentence;
    g.members.push_back(live[static_cast<size_t>(i)]->text);
  }

  for (NameGroup& g : out) {
    // THE DISPLAY NAME IS THE MOST-MENTIONED MEMBER, so what is ON THE PAGE is what
    // the list shows and therefore what a reader can find.
    std::sort(g.members.begin(), g.members.end(),
              [&](const std::string& a, const std::string& b) {
                int na = 0, nb = 0;
                for (const NameIndexEntry* e : live) {
                  if (e->text == a) na = e->midSentence;
                  if (e->text == b) nb = e->midSentence;
                }
                if (na != nb) return na > nb;
                return a < b;
              });
    g.display = g.members.front();
    // THE FULLEST FORM IS THE LONGEST MEMBER: more tokens first, then longer. It is
    // often most of the answer before an extract is read.
    const std::string* fullest = &g.members.front();
    for (const std::string& m : g.members) {
      const size_t tm = tokenCount(m), tf = tokenCount(*fullest);
      if (tm > tf || (tm == tf && m.size() > fullest->size())) fullest = &m;
    }
    // EMPTY WHERE THERE IS NOTHING LONGER TO REVEAL, which is the short row on the
    // board. Repeating the display name underneath would be the only thing worse
    // than leaving it blank.
    if (*fullest != g.display) g.fullest = *fullest;
  }

  // ALPHABETICAL BY DISPLAY NAME. You always arrive knowing the string, because you
  // just read it, and alphabetical is the only order where knowing it tells you
  // where to look.
  std::sort(out.begin(), out.end(), [](const NameGroup& a, const NameGroup& b) {
    if (a.display != b.display) return a.display < b.display;
    return a.midSentence > b.midSentence;
  });
  return out;
}

}  // namespace reader
