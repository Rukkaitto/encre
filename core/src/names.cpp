#include "reader/names.h"

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
  Run r;
  r.text.assign(text);
  return &*runs_.insert(it, std::move(r));
}

void NameScanner::addBlock(const Block& block, int blockInChapter) {
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
        if (Run* r = findOrAdd(run)) {
          ++r->total;
          // SENTENCE-INITIAL SUPPRESSION, the load-bearing rule. `i != 0` is position
          // and `!opener` is grammar; a mention needs both to count.
          if (i != 0 && !toks[i].opener) ++r->midSentence;
          if (blockInChapter <= 1) ++r->chapterOpening;
        }
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

}  // namespace reader
