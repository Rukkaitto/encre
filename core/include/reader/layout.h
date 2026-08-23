#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/components.h"  // Prose
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
  // OWNED, not a view, and that decision is what makes a streaming reader
  // tractable. A view meant the Document -- or, once chapters stream, whichever
  // blocks the page happened to span -- had to be kept alive for exactly as long as
  // the Page was, which is a lifetime rule the reader would have to enforce across
  // a page turn while blocks are being discarded behind it.
  //
  // A page is ~12 lines of ~45 bytes. Copying them costs about 1 KB and a dozen
  // small allocations per page turn, against a ~520 ms panel refresh -- and it
  // makes a Page self-contained, so a block can be dropped the moment its lines
  // have been taken. That is the whole reason nothing here needs a block window.
  std::string text;
  int x = 0;          // px, left edge; carries the indent
  int baselineY = 0;  // px
  // What drawTextJustified should add after each ASCII space, or 0 for a line set
  // ragged -- which is every last line of a paragraph, every line with no gaps,
  // and every line too empty to justify (see kMinJustifyFillPercent).
  int extraPerGapF26 = 0;
  BlockKind kind = BlockKind::Paragraph;

  // WHICH BLOCK THIS LINE CAME FROM, and whether it is that block's last line.
  //
  // Carried rather than inferred. Two tests used to recover it by comparing
  // `text.data()` pointers, which worked only while every line was a view into one
  // contiguous buffer -- so making a Page own its text broke them, and the break
  // was in the tests' technique rather than in the layout. It is also exactly what
  // a page index keys on: a Cursor is a block and a line within it.
  int block = 0;
  bool lastOfBlock = false;
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

// PAGES FROM A STREAM OF BLOCKS, which is the engine everything else here is
// built on -- `layoutPage` below is this fed from an in-memory Document.
//
// It exists because pagination cannot ask for block N: blocks arrive from a
// BlockReader in order, once, and are dropped behind it. So the caller pushes and
// this pulls pages out:
//
//     PageBuilder pb(font, metrics);
//     Block b;
//     while (reader.next(b)) {
//       pb.add(b, index++);
//       while (pb.ready()) consume(pb.take());
//     }
//     consume(pb.finish());
//
// `add` may only be called when `ready()` is false -- that is the signal that the
// block just fed has been fully laid out. A block longer than a page fills several,
// which is why `ready()` is a loop and not an if.
//
// ITS MEMORY IS ONE BLOCK PLUS ONE PAGE. The block's text is copied in, because
// the wrap produces views into it and they must outlive the block the caller is
// about to drop; a page's lines then own their own text (see LaidLine), so the
// block is released the moment its last line is laid. Measured over a real book:
// the largest block is 4,406 bytes and a page is ~1 KB of line text.
class PageBuilder {
 public:
  // `font` and `m` must outlive the builder.
  PageBuilder(const GlyphSource& font, const PageMetrics& m);

  // Feed the next block. `index` is its global position in the chapter, which is
  // what a Cursor names. Blocks must arrive in order with none skipped: the indent
  // rule reads the previous block's kind, and a page cursor is meaningless if the
  // count has a hole in it.
  void add(const Block& b, int index);

  // A page has filled. Take it and keep going; whatever is left of the block that
  // overflowed carries onto the next page.
  bool ready() const;
  Page take();

  // The blocks have run out. The partial page, marked as the last.
  Page finish();

  // AN INDEX PASS WANTS PAGE BOUNDARIES, NOT PAGES. In counting mode the builder
  // still wraps every block -- that is what decides where a page ends -- but it does
  // not build a LaidLine: no owned string per line, and no justification measure.
  //
  // Both of those are pure waste when the pages are discarded, and there is a lot of
  // them: a 40-page chapter is ~480 lines, so 480 string copies and 480 extra walks
  // over every glyph of the chapter (stretchFor calls measure() on the finished
  // line, on top of the measures the greedy wrap already did). The device reported a
  // 40-page chapter taking about two seconds to open against near-instant page
  // turns; this is the half of that which was avoidable.
  //
  // Set it before the first add(). pageStart() still tracks, take() and finish()
  // still delimit pages -- they just hand back empty ones.
  void countOnly() { linesWanted_ = false; }

  // False when the column cannot hold even one line box. A caller must check it
  // rather than loop on an empty page -- see layoutPage.
  bool viable() const { return rows_ > 0; }

  // Where the page currently being built began. This is what a page index records.
  Cursor pageStart() const { return pageStart_; }

  // Whether the page being built has anything on it yet.
  //
  // A caller cannot ask the Page instead: in counting mode `finish()` hands back a
  // page with no lines whether or not there was one, so a trailing partial page was
  // silently dropped from the index -- which for a chapter that fits on one page
  // meant an index with NOTHING in it and a Reader reporting 0 pages.
  bool pageHasContent() const { return row_ > 0; }

  // Discards everything before `at`, then begins a page there. For `layoutPage`'s
  // "a page starting exactly HERE" semantics, which is not necessarily a boundary
  // the natural pagination would have chosen.
  void startAt(Cursor at);

 private:
  void beginPage();
  void drain();

  const GlyphSource* font_;
  PageMetrics m_;
  int leadF26_ = 0;
  int indentF26_ = 0;
  int rows_ = 0;

  // The block being laid out, owned -- see the class comment.
  std::string held_;
  Prose prose_{};
  BlockKind kind_ = BlockKind::Paragraph;
  BlockKind prevKind_ = BlockKind::Heading;  // a break precedes the first block
  int blockIndex_ = 0;
  int line_ = 0;
  bool haveBlock_ = false;
  bool indentThis_ = false;
  bool linesWanted_ = true;

  Page page_{};
  int row_ = 0;
  Cursor pageStart_{};

  // While set, output is discarded until this cursor is reached. startAt's whole
  // mechanism.
  bool skipping_ = false;
  Cursor skipTo_{};
};

// One page of `doc`, starting at `from`.
//
// A PAGE BOUNDARY MAY LAND INSIDE A PARAGRAPH, and must: a 20-line page and a
// 9-line paragraph means most pages end mid-paragraph, so treating a paragraph as
// unbreakable would leave pages a third empty. `next.line` is how the following
// page resumes mid-wrap.
//
// NOT FOR A LOOP. It feeds a PageBuilder from the Document's first block every
// call, because there is one set of layout rules and the builder holds them -- so
// walking a chapter with this is quadratic in the page count. The reader drives a
// PageBuilder directly and pays one page per turn; this is for a caller that has a
// whole Document and wants one page of it.
//
// No widow or orphan control. It is the obvious next refinement and it is not in
// V1: the rules trade blank space at the foot of a page for a tidier break, and on
// a 20-line page that trade is expensive enough to want measuring before it is
// made.
Page layoutPage(const Document& doc, const GlyphSource& font, const PageMetrics& m, Cursor from);

}  // namespace reader
