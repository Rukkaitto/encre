#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reader {

// --- Which bytes of a string are emphasised ----------------------------------
//
// Inline emphasis, as BYTE RANGES beside the text rather than as a run-per-style
// structure the text is broken into. That choice is the whole design, and it is
// made for the case that dominates: measured over eight real books, `<em>` covers
// 0.6% to 5.8% of a chapter's characters, so **almost every line has none**. An
// empty `std::vector<Span>` costs nothing and the un-emphasised path is untouched
// -- where a vector of styled runs per line would allocate on every line of every
// page to say "all roman".
//
// It also keeps `text` ONE contiguous string, which the layout depends on far more
// than it looks. Justification counts ASCII spaces in the line and divides the
// slack between them (`extraPerGapF26`); the wrap measures substrings; a Cursor is
// a block and a line index. Breaking the text into runs would put a seam through
// all three.
//
// OFFSETS ARE ALWAYS RELATIVE TO THE STRING THEY TRAVEL WITH, and they are
// re-based rather than shared: a Block's spans index the block's text, a LaidLine's
// spans index that line's own text. `clipTo` below is what re-bases them, and it
// exists because the alternative -- a line holding offsets into a block it does not
// own -- is the lifetime bug this project has already shipped once, where
// `Prose::lines` were views into a temporary.
struct Span {
  uint32_t off = 0;
  uint32_t len = 0;

  uint32_t end() const { return off + len; }
  bool operator==(const Span& o) const { return off == o.off && len == o.len; }
};

// A file may claim anything, so the count is capped like every other quantity this
// parser reads off a card. Eight real books peak at 997 `<em>` runs in a whole BOOK
// (Dune 4) against 7,138 paragraphs, so a block wanting more than this is not a
// book with a lot of emphasis -- it is a file worth refusing.
inline constexpr size_t kMaxEmphasisPerBlock = 256;

// Whether byte `off` falls inside any span. Linear, and deliberately: the vector is
// almost always empty and never long, so a binary search would cost a branch to
// save nothing.
inline bool emphasisedAt(const std::vector<Span>& spans, size_t off) {
  for (const Span& s : spans)
    if (off >= s.off && off < s.end()) return true;
  return false;
}

// Where the style changes at or after `off`, or `limit` if it does not change
// again. This is what lets a measure or a draw walk a range in as few pieces as
// there are style changes in it -- one piece for the overwhelming majority of
// lines.
//
// Spans are assumed sorted and non-overlapping, which is what `collectEmphasis`
// below guarantees; an unsorted vector makes this return a nearer boundary than it
// should, which costs an extra piece and no correctness.
inline size_t nextStyleBoundary(const std::vector<Span>& spans, size_t off, size_t limit) {
  size_t best = limit;
  for (const Span& s : spans) {
    if (off < s.off && s.off < best) best = s.off;              // roman -> italic
    if (off >= s.off && off < s.end() && s.end() < best) best = s.end();  // italic -> roman
  }
  return best;
}

// The spans of `src` that fall in [from, to), re-based so that `from` becomes 0 --
// which is exactly what a line needs when it is cut out of a block.
//
// PARTIAL OVERLAPS ARE CLIPPED, NOT DROPPED, and that is the case that matters: an
// emphasised phrase that wraps across a line break ("poor / dress") is the whole
// reason a run model is harder than a flag, and dropping the overlap would render
// the first half italic and the second half roman.
inline std::vector<Span> clipTo(const std::vector<Span>& src, size_t from, size_t to) {
  std::vector<Span> out;
  if (to <= from) return out;
  for (const Span& s : src) {
    const size_t a = s.off > from ? s.off : from;
    const size_t b = s.end() < to ? s.end() : to;
    if (a < b)
      out.push_back(Span{static_cast<uint32_t>(a - from), static_cast<uint32_t>(b - a)});
  }
  return out;
}

}  // namespace reader
