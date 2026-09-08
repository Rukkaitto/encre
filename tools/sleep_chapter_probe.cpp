// EVERY CHAPTER NAME IN THE CORPUS, AGAINST THE SLEEP CARD'S OWN COLUMN.
//
// The card's last line was `6% - CH. 01`: a percentage and a SPINE POSITION. This
// is the probe that decided what replaces the position -- whether a real chapter
// NAME fits that run, how often it does not, and therefore whether the name may
// elide on one line, has to be allowed a second, or needs a wider line than the
// combined run can give it.
//
// It exists because design/Sleep.dc.html's author cap exists for a measurement of
// exactly this shape -- that cap is two lines because 68 of 221 corpus authors
// overflow one and 58 of those 68 fit WHOLE in two -- and a cap picked without
// those two figures is not a cap. This is the same question one run lower, and it
// came back with a DIFFERENT answer: see WHAT IT FOUND.
//
// WHAT IT MEASURES, and every choice is the shipped renderer's rather than a
// convenient approximation:
//
//   * Role::Label500, which is what QuietTheme::renderSleep draws this run with
//     and the only 23px weight the ramp carries (font_manifest.h has Label400 and
//     Label500 and no Label700).
//   * Against `contentW` = 312px: kSleepCardMaxW 400, less two 2px borders and two
//     42px paddings. It is 312 on BOTH panels, because 400 is under the X4's
//     480 - 2*kMargin as well, so there is one figure here and not two.
//   * At three trackings, because the run's own 0.14em was a COUNTER's tracking
//     and a name is not a counter -- Home reached that conclusion for the identical
//     string and gave it 0.10em.
//   * At the COMBINED run's budget as well as at the full column: the shipped run
//     is `NN% - <name>`, so a name sharing it gets the column less the percentage's
//     own width, and the percentage may not shrink because it is the quantity.
//     Both prefixes are measured -- `6% - ` (the board's) and `100% - ` (the widest
//     a device can produce) -- so the answer cannot depend on which book.
//   * As authored AND shouted, because the card shouts its title and its author and
//     whether this run joins them is a real question.
//
// EVERY TOC LABEL COUNTS, not just the one a given book is open at: a reader can
// sleep in any chapter, so the distribution over all labels is the question, and
// the worst label per BOOK is what says whether a book has a bad case at all.
//
// IT ALSO MEASURES THE TITLE, which is the run any answer here is paid for out of.
// The card's height is a sum and the title takes what is left, so one more run
// costs the title budget one line; how much that matters is a fact about real
// titles and not an argument.
//
// WHAT IT FOUND, over 225 books (205 with a usable NCX, 8,617 labels):
//
//   * A CHAPTER NAME DOES NOT FIT, AND IT IS NOT CLOSE. p50 231px, p90 571px,
//     max 2005px against a 312px column -- so the MEDIAN label overflows the
//     combined run and the 90th percentile is 1.8x the whole card.
//   * SO THE RUN HAD TO SPLIT. The name elides on 52.51% of labels when it shares
//     the row with `100% - `, and on 34.54% given its own line at 0.10em. 34.54%
//     is the band Home already accepted for the same string (30.70% on the X4);
//     more than half is not.
//   * AND IT MAY NOT WRAP, which is where this diverges from the author. Of the
//     labels over the full column, 26.76% need THREE lines or more, so a two-line
//     cap would still elide 10.06% of all labels and would spend up to two of the
//     title's lines doing it. The author's cap was earned by 85% of its overflows
//     fitting two; this distribution has no such knee.
//   * THE TITLE CAN AFFORD THE ONE LINE IT COSTS: 2 of 225 titles need exactly 8
//     lines and so newly elide at a 7-line budget, and 4 already elided at 8.
//
//   build: cmake --build build --target sleep_chapter_probe
//   run:   build/sleep_chapter_probe assets/built ~/.cache/encre-corpus/*/*.epub
//
// NOT in ctest, for corpus_probe's and name_probe's reason: it needs real books,
// and the repo's generated EPUBs are stubs whose one-word labels fit everything.

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "reader/book.h"
#include "reader/components.h"
#include "reader/font_manifest.h"
#include "reader/fontset.h"
#include "reader/host_fs.h"
#include "reader/text.h"
#include "reader/toc.h"

namespace {

// The card's content column, as theme_quiet.cpp derives it. Restated rather than
// included because those constants live in that file's anonymous namespace; the
// arithmetic is one line, and the probe printing it is what says it is still 312.
constexpr int kCardMaxW = 400;
constexpr int kCardPadX = 42;
constexpr int kCardBorder = 2;
constexpr int kContentW = kCardMaxW - 2 * (kCardBorder + kCardPadX);

// The board's own tracking on this run, the tracking Home gives a chapter NAME,
// and one step between them.
constexpr int kCounterEm = 140;
constexpr int kNameEm = 100;

// Home's budget for the identical string, as the control: `titleW` is
// fb.width() - kMargin - (kMargin + kCoverW + kGutter), so 304 on the X4 and 352
// on the X3, drawn at Meta400 and kTightMetaEm.
constexpr int kHomeWX4 = 304;
constexpr int kHomeWX3 = 352;

// The title's line box on this card -- kSleepTitleLineH, the board's 1.1 on 42px.
constexpr int kTitleLineH = 46;

std::vector<uint8_t> slurp(const std::string& path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return out;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n > 0) {
    out.resize(static_cast<size_t>(n));
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  }
  std::fclose(f);
  return out;
}

// The ramp, with its bytes beside it: a FontSet owns nothing, so the blobs must
// outlive it. sim/main.cpp's SimRamp, over the same manifest.
struct Ramp {
#define ENCRE_ROLE_BLOB(role, stem) std::vector<uint8_t> blob_##role;
  READER_FONT_RAMP(ENCRE_ROLE_BLOB)
#undef ENCRE_ROLE_BLOB
  reader::FontSet fonts;
};

bool loadRamp(Ramp& r, const std::string& dir) {
#define ENCRE_LOAD_ROLE(role, stem)           \
  r.blob_##role = slurp(dir + #stem ".rfnt"); \
  r.fonts.load(reader::Role::role, r.blob_##role.data(), r.blob_##role.size());
  READER_FONT_RAMP(ENCRE_LOAD_ROLE)
#undef ENCRE_LOAD_ROLE
  return r.fonts.ready();
}

// Every TOC label in the corpus, read once and kept: half a dozen of the questions
// below walk the same list, and re-opening 225 archives per question is minutes.
std::vector<std::string> collectLabels(reader::FileSystem& fs, int argc, char** argv,
                                       size_t& booksOut, size_t& withTocOut,
                                       std::vector<std::vector<std::string>>& perBook) {
  std::vector<std::string> all;
  for (int i = 2; i < argc; ++i) {
    ++booksOut;
    std::vector<reader::TocEntry> toc;
    const char* reason = nullptr;
    if (!reader::loadToc(fs, argv[i], toc, &reason) || toc.empty()) continue;
    ++withTocOut;
    std::vector<std::string> mine;
    for (const reader::TocEntry& e : toc) {
      all.push_back(e.label);
      mine.push_back(e.label);
    }
    perBook.push_back(std::move(mine));
  }
  return all;
}

size_t countOver(const reader::GlyphSource& f, const std::vector<std::string>& labels,
                 reader::Tracking t, int budget) {
  size_t n = 0;
  for (const std::string& s : labels) {
    if (f.measure(s, t) > budget) ++n;
  }
  return n;
}

void report(const char* what, size_t over, size_t total) {
  std::printf("  %-46s %6zu / %zu  (%5.2f%%)\n", what, over, total,
              total ? 100.0 * static_cast<double>(over) / static_cast<double>(total) : 0.0);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: sleep_chapter_probe <assets/built> <book.epub>...\n");
    return 2;
  }
  Ramp ramp;
  if (!loadRamp(ramp, std::string(argv[1]) + "/")) {
    std::fprintf(stderr, "font ramp failed to load from %s\n", argv[1]);
    return 2;
  }
  const reader::Font& f = ramp.fonts[reader::Role::Label500];
  const reader::Tracking counter = reader::trackingEm(f, kCounterEm);
  const reader::Tracking name = reader::trackingEm(f, kNameEm);

  // The prefixes, with the same two-byte middot the firmware emits.
  const std::string dot = "\xC2\xB7";
  const int prefix6 = f.measure("6% " + dot + " ", counter);
  const int prefix100 = f.measure("100% " + dot + " ", counter);

  std::printf("column=%d  Label500 ppem=%d lineHeight=%d\n", kContentW, f.ppem(),
              f.lineHeight());
  std::printf("combined-run prefixes: 6%%=%d  100%%=%d  -> name budgets %d / %d\n", prefix6,
              prefix100, kContentW - prefix6, kContentW - prefix100);

  reader::HostFileSystem fs("/");
  size_t books = 0, withToc = 0;
  std::vector<std::vector<std::string>> perBook;
  const std::vector<std::string> labels = collectLabels(fs, argc, argv, books, withToc, perBook);
  const size_t n = labels.size();
  std::printf("books=%zu withToc=%zu labels=%zu\n\n", books, withToc, n);

  // --- The distribution -------------------------------------------------------
  std::vector<int> widths;
  widths.reserve(n);
  for (const std::string& s : labels) widths.push_back(f.measure(s, name));
  std::sort(widths.begin(), widths.end());
  const auto at = [&](double p) {
    if (widths.empty()) return 0;
    size_t k = static_cast<size_t>(p / 100.0 * static_cast<double>(widths.size()));
    if (k >= widths.size()) k = widths.size() - 1;
    return widths[k];
  };
  std::printf("label widths px @ 0.%03dem: min=%d p50=%d p75=%d p90=%d p95=%d p99=%d max=%d\n",
              kNameEm, widths.empty() ? 0 : widths.front(), at(50), at(75), at(90), at(95),
              at(99), widths.empty() ? 0 : widths.back());

  // --- How often each candidate budget elides ---------------------------------
  std::printf("\nlabels that do not fit one line:\n");
  report("combined run, 100% prefix, 0.14em",
         countOver(f, labels, counter, kContentW - prefix100), n);
  report("combined run, 6% prefix, 0.14em", countOver(f, labels, counter, kContentW - prefix6), n);
  report("own line, full column, 0.14em", countOver(f, labels, counter, kContentW), n);
  report("own line, full column, 0.12em",
         countOver(f, labels, reader::trackingEm(f, 120), kContentW), n);
  report("own line, full column, 0.10em", countOver(f, labels, name, kContentW), n);
  {
    std::vector<std::string> shouted;
    shouted.reserve(n);
    for (const std::string& s : labels) shouted.push_back(reader::upperLatin1(s));
    report("own line, full column, 0.10em, SHOUTED", countOver(f, shouted, name, kContentW), n);
  }
  {
    const reader::Font& mf = ramp.fonts[reader::Role::Meta400];
    const reader::Tracking mt = reader::trackingEm(mf, reader::kTightMetaEm);
    report("HOME control: Meta400/0.10em, X4 titleW 304", countOver(mf, labels, mt, kHomeWX4), n);
    report("HOME control: Meta400/0.10em, X3 titleW 352", countOver(mf, labels, mt, kHomeWX3), n);
  }

  // --- Would a second line rescue it, the way it rescued the author? ----------
  //
  // The REAL wrap, not width/column: greedy on spaces, and a label whose single
  // token is wider than the column takes more lines than its width implies.
  {
    size_t over = 0, two = 0, three = 0;
    for (const std::string& s : labels) {
      if (f.measure(s, name) <= kContentW) continue;
      ++over;
      const reader::Prose p = reader::wrapProseLead(
          f, s, kContentW, reader::pxToF26(f.lineHeight()), name, reader::WordBreak::Anywhere);
      if (p.lines.size() <= 2) {
        ++two;
      } else {
        ++three;
      }
    }
    std::printf("\nof the %zu labels over the full column at 0.%03dem, wrapped:\n", over, kNameEm);
    std::printf("  %zu fit two lines whole (%.2f%%), %zu need three or more (%.2f%%)\n", two,
                over ? 100.0 * static_cast<double>(two) / static_cast<double>(over) : 0.0, three,
                over ? 100.0 * static_cast<double>(three) / static_cast<double>(over) : 0.0);
    std::printf("  so a TWO-LINE cap would still elide %zu of %zu labels (%.2f%%)\n", three, n,
                n ? 100.0 * static_cast<double>(three) / static_cast<double>(n) : 0.0);
  }

  // --- Per book: does any of its labels overflow ------------------------------
  {
    size_t allFit = 0;
    std::vector<std::pair<int, std::string>> worst;
    for (const std::vector<std::string>& b : perBook) {
      int w = 0;
      std::string label;
      for (const std::string& s : b) {
        const int m = f.measure(s, name);
        if (m > w) {
          w = m;
          label = s;
        }
      }
      if (w <= kContentW) ++allFit;
      worst.emplace_back(w, label);
    }
    std::printf("\nbooks whose EVERY label fits the full column: %zu / %zu (%.2f%%)\n", allFit,
                withToc,
                withToc ? 100.0 * static_cast<double>(allFit) / static_cast<double>(withToc) : 0.0);
    std::sort(worst.begin(), worst.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::printf("widest label per book, top 10:\n");
    for (size_t i = 0; i < worst.size() && i < 10; ++i) {
      std::printf("  %6d  %.90s\n", worst[i].first, worst[i].second.c_str());
    }
  }

  // --- What the new run costs the TITLE ---------------------------------------
  //
  // The card's height is a sum and the title takes the remainder, so one more run
  // is one fewer title line. Whether that matters is a fact about real titles.
  {
    const reader::Font& tf = ramp.fonts[reader::Role::Title700];
    std::vector<size_t> lines;
    for (int i = 2; i < argc; ++i) {
      reader::OpenedBook b;
      const char* why = nullptr;
      if (!reader::openBook(fs, argv[i], b, &why)) continue;
      const std::string shouted = reader::upperLatin1(b.title);
      const reader::Prose p =
          reader::wrapProseLead(tf, shouted, kContentW, reader::pxToF26(kTitleLineH), {},
                                reader::WordBreak::Anywhere);
      lines.push_back(p.lines.size());
    }
    std::sort(lines.begin(), lines.end());
    size_t exactly8 = 0, over8 = 0;
    for (const size_t k : lines) {
      if (k == 8) ++exactly8;
      if (k > 8) ++over8;
    }
    std::printf("\ntitles wrapped at %dpx over the same column: n=%zu p50=%zu p90=%zu max=%zu\n",
                kTitleLineH, lines.size(), lines.empty() ? 0 : lines[lines.size() / 2],
                lines.empty() ? 0 : lines[lines.size() * 9 / 10], lines.empty() ? 0 : lines.back());
    std::printf("  needing EXACTLY 8 lines: %zu  <- what an 8->7 budget newly elides\n", exactly8);
    std::printf("  already over 8: %zu  <- these elided before this change too\n", over8);
  }
  return 0;
}
