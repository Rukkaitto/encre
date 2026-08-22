#pragma once
#include <string_view>
#include <vector>

#include "reader/document.h"
#include "reader/tracking.h"

namespace reader {
class GlyphSource;

// A chapter into PAGES: which lines, where, and how much each one stretches.
//
// It measures and it does not draw. Everything here goes through
// GlyphSource::advance(), never glyph() -- design decision 3 of Phase 3A, and this
// is the layer it was made for. A scalable face rasterises inside glyph() at
// ~3,794us a glyph; a page holds ~600 of them, so a layout pass that rasterised
// would cost 2.3 seconds to decide where to break a line it has not drawn yet.
// test_layout.cpp asserts it on the cache's own counter rather than trusting the
// discipline: a full pass must leave `cacheStats().rasterisations` at zero.
//
// ONE PAGE AT A TIME, from a cursor, rather than a whole chapter paginated up
// front. A chapter is ~2.5 KB of text and 90 line boxes, which is affordable --
// but the page after this one is the only page the reader can reach, so laying out
// the other twenty is work whose result is discarded on a book that gets closed.
// The cost is that the paragraph straddling a page boundary is re-wrapped when the
// reader lands on the next page, and re-wrapping is advances only.
//
// LINE BREAKING IS NOT HERE. It is wrapProseLead in reader/components.h, which
// already breaks greedily on ASCII spaces against these exact metrics, and a
// second implementation for body text would inherit none of the fixes that one
// has had. What IS here is the two things a page needs and a paragraph does not:
// justification, and where the page ends.

// The board's own numbers -- design/Reader.dc.html.
inline constexpr int kBodyPpem = 32;         // `font-size: 32px`
inline constexpr int kBodyLeadEm = 1700;     // `line-height: 1.7`
inline constexpr int kBodyIndentEm = 1500;   // `text-indent: 1.5em` on a continuing paragraph

// How full a line must be, as a percentage of its column, before it is justified
// at all. Below this it is set ragged.
//
// THE TEST IS THE LINE, NOT THE GAP, and getting that round the wrong way is
// instructive. This started as a cap on how far one gap could stretch -- three
// times the space's own width -- on the reasoning that justification's failure
// case is a corridor of white between two words. The failure case is real: our
// own fixtures contain "pneumonoultramicroscopicsilicovolcanoconiosis", which is
// wider than the 444px column, so the greedy wrap puts it alone on a line and
// leaves the line before it holding two words and 330px of slack.
//
// But a per-gap cap cannot tell that line from ordinary prose, because the number
// of gaps is what converts slack into stretch. Measured on
// design/Reader.dc.html's own two paragraphs, the cap refused "necklace, and the
// two of" -- 367px of text in a 444px column, a perfectly ordinary line -- because
// its 77px of slack fell across only four gaps, 19.25px each against an 18px cap.
// It refused it BY ONE PIXEL, and set it ragged directly beneath a line it had
// justified at 15.25px. A ragged line sitting between two justified ones is
// exactly what the cap existed to avoid, arrived at from the other direction.
//
// A line that is 83% full is prose. A line that is 23% full is the corridor. So
// the question is how much of the line is TEXT, which is the thing actually
// visible, and it needs no reference to the gap count at all.
//
// 60% is where "more text than space" stops being true. Measured over the same
// 6,800 pages of tools/mkepub.py output: the per-gap cap set 18% of all lines
// ragged and the fill test sets 3.4%, taking justified lines from 71% to 86%. The
// lines that remain ragged are almost all paragraph-final, which is where ragged
// belongs.
//
// The price is admitted rather than hidden: a line at the threshold has 40% of its
// column as slack, and across four gaps that is a gap five or six times the space's
// own width -- a visible river. That is the trade a wrap with no hyphenation
// dictionary has to make, and it is made in this direction because an occasional
// wide gap reads as loose typesetting while a ragged line mid-paragraph reads as
// the feature being broken.
inline constexpr int kMinJustifyFillPercent = 60;

struct PageMetrics {
  // The column in FRAMEBUFFER coordinates, so a LaidLine's x and baselineY are
  // absolute and can be handed straight to drawTextJustified. Layout used to
  // report them relative to the column and let the renderer add the origin; that
  // is one addition per line in the caller and one chance per caller to forget it.
  int columnLeft = 0;
  int columnTop = 0;
  int columnW = 0;  // px -- the board's 480 less its 18px side padding, so 444
  int columnH = 0;  // px available between the header and the footer
  int leadEm1000 = kBodyLeadEm;
  int indentEm1000 = kBodyIndentEm;
  Tracking tracking{};
};

// Where a page begins: a block, and a line within that block's wrap.
//
// The line index is meaningful only for the (font, columnW) that produced it. That
// is not a hazard in practice -- changing either is a re-layout of the book -- but
// it is why a saved reading position is a BLOCK and not a page number.
struct Cursor {
  int block = 0;
  int line = 0;
  bool operator==(const Cursor& o) const { return block == o.block && line == o.line; }
  bool operator!=(const Cursor& o) const { return !(*this == o); }
};

struct LaidLine {
  // A view into the Document's own text, so the Document must outlive the Page.
  std::string_view text;
  int x = 0;          // px, left edge; carries the indent
  int baselineY = 0;  // px
  // What drawTextJustified should add after each ASCII space, or 0 for a line set
  // ragged -- which is every last line of a paragraph, every line with no gaps,
  // and every line too empty to justify (see kMinJustifyFillPercent).
  int extraPerGapF26 = 0;
  BlockKind kind = BlockKind::Paragraph;
};

struct Page {
  std::vector<LaidLine> lines;
  // Where the NEXT page begins. Equal to the page's own start only if nothing
  // fit, which happens when columnH is smaller than one line box.
  Cursor next;
  // Nothing follows. The caller needs this to know a page turn is an end-of-book
  // rather than a blank screen.
  bool lastPage = false;
};

// One page of `doc`, starting at `from`.
//
// A PAGE BOUNDARY MAY LAND INSIDE A PARAGRAPH, and must: a 20-line page and a
// 9-line paragraph means most pages end mid-paragraph, so treating a paragraph as
// unbreakable would leave pages a third empty. `next.line` is how the following
// page resumes mid-wrap.
//
// No widow or orphan control. It is the obvious next refinement and it is not in
// V1: the rules trade blank space at the foot of a page for a tidier break, and on
// a 20-line page that trade is expensive enough to want measuring before it is
// made.
Page layoutPage(const Document& doc, const GlyphSource& font, const PageMetrics& m, Cursor from);

}  // namespace reader
