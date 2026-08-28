#include "reader/layout.h"

#include "reader/components.h"
#include "reader/glyphsource.h"
#include "reader/text.h"

namespace reader {
namespace {

// Whether this block's first line is pushed in.
//
// A PARAGRAPH IS INDENTED ONLY IF THE ONE BEFORE IT WAS ALSO A PARAGRAPH. An
// indent means "this continues what you were reading", so the first paragraph of a
// chapter has nothing to continue, and neither does one following a heading, a
// quote or a list -- the intervening block is itself the break the indent would be
// announcing. That is the convention design/Reader.dc.html states and the reason
// mkepub.py marks its chapter openers `class="first"`.
//
// A paragraph after a blockquote is the case the rule decides arbitrarily: a print
// compositor indents it if it starts a new thought and sets it flush if the quote
// interrupted one, and nothing in the markup distinguishes those. Flush is chosen
// because it is the answer that is never obtrusive -- a missing indent reads as
// continuation, an unwanted one reads as a paragraph that is not there.
bool indentedAfter(BlockKind prev, BlockKind here, bool isFirst) {
  if (isFirst) return false;
  return here == BlockKind::Paragraph && prev == BlockKind::Paragraph;
}

// Whether this kind's lines are justified at all.
//
// Paragraphs and quotes are body text and the board says `text-align: justify`.
// Headings and list items are NOT: a heading is a display line whose slack is
// meant to be visible, and a list item is usually short enough that justifying it
// opens the corridor kMinJustifyFillPercent exists to prevent -- on every line
// rather than occasionally.
bool justifiable(BlockKind k) {
  return k == BlockKind::Paragraph || k == BlockKind::Blockquote;
}

// How much each ASCII space on this line stretches, or 0 for ragged.
//
// The gap COUNT here and the codepoint drawTextJustified stretches must be the
// same rule, which is why both name U+0020 and nothing else.
int stretchFor(const GlyphSource& font, std::string_view line, int availW, Tracking tracking) {
  int gaps = 0;
  for (const char c : line)
    if (c == ' ') ++gaps;
  if (gaps == 0) return 0;  // one long word: nothing to distribute across

  const int naturalW = font.measure(line, tracking);
  // Negative slack is a word wider than the column, which the wrap deliberately
  // let overhang. Pulling the gaps tighter to compensate would compress a line
  // that is already wrong, in a way that looks like a different bug.
  if (naturalW >= availW) return 0;

  // The ragged fallback, tested on how full the LINE is rather than on how far a
  // gap would stretch -- see kMinJustifyFillPercent for why that distinction is
  // the whole of it. Multiplied out rather than divided, so a narrow column needs
  // no rounding rule of its own.
  if (naturalW * 100 < availW * kMinJustifyFillPercent) return 0;
  return pxToF26(availW - naturalW) / gaps;
}

// HOW MANY BLANK ROWS GO ABOVE THIS BLOCK.
//
// Rows, not pixels, and that is the constraint the whole design is built on rather
// than a simplification: `rows_ = columnH / lead` and this advances an integer
// `row_`, so space between blocks can only be a WHOLE line box. A heading with 0.6
// of a row beneath it is not expressible, and asking for one would put the board and
// the firmware in disagreement about where every line after it sits.
//
// The rule reproduces both styled boards exactly, which is how it was chosen rather
// than invented: ReaderChapterOpen is heading + blank + quote(2) + blank + prose(7)
// = 12 rows, and ReaderList is prose(3) + blank + list(4) + blank + prose(3) = 12.
int blankRowsBefore(BlockKind prev, BlockKind here, bool isFirst) {
  // Nothing above it to be separated from, and a page that opened with a blank row
  // would look like a rendering fault.
  if (isFirst) return 0;
  if (here == BlockKind::Heading || prev == BlockKind::Heading) return 1;
  if (here == BlockKind::Blockquote || prev == BlockKind::Blockquote) return 1;
  // ENTERING OR LEAVING a list, never BETWEEN its items: a blank row between every
  // item makes a three-item list read as three paragraphs.
  if ((here == BlockKind::ListItem) != (prev == BlockKind::ListItem)) return 1;
  return 0;
}

}  // namespace

PageBuilder::PageBuilder(const GlyphSource& font, const PageMetrics& m)
    : font_(&font), m_(m) {
  leadF26_ = Tracking::em(font.ppem(), m.leadEm1000).f26();
  indentF26_ = Tracking::em(font.ppem(), m.indentEm1000).f26();
  quoteInsetPx_ = f26ToPx(Tracking::em(font.ppem(), kQuoteInsetEm).f26());
  listHangPx_ = f26ToPx(Tracking::em(font.ppem(), kListHangEm).f26());
  // A column that cannot hold one line box yields no pages at all, rather than
  // dividing by zero -- layout.h says a caller must not loop on that.
  rows_ = (leadF26_ > 0 && m.columnW > 0) ? pxToF26(m.columnH) / leadF26_ : 0;
  beginPage();
}

// A BLOCK'S OWN COLUMN. Everything else on the page uses the page's.
int PageBuilder::columnLeftFor(BlockKind k) const {
  if (k == BlockKind::Blockquote) return m_.columnLeft + quoteInsetPx_;
  // EVERY LINE of a list item, including the first: the text runs in the narrower
  // measure and only the MARKER sits out at the margin. That is what a hanging indent
  // is, and it is why the first line is not a special case here.
  if (k == BlockKind::ListItem) return m_.columnLeft + listHangPx_;
  return m_.columnLeft;
}

int PageBuilder::columnWFor(BlockKind k) const {
  if (k == BlockKind::Blockquote) return m_.columnW - 2 * quoteInsetPx_;
  if (k == BlockKind::ListItem) return m_.columnW - listHangPx_;
  return m_.columnW;
}

void PageBuilder::beginPage() {
  page_ = Page{};
  row_ = 0;
  pageStart_ = Cursor{blockIndex_, line_};
}

void PageBuilder::startAt(Cursor at) {
  skipping_ = true;
  skipTo_ = at;
}

void PageBuilder::add(const Block& b, int index) {
  // The text is COPIED, because the wrap below produces views into it and the
  // caller is free to drop its block the moment this returns.
  held_ = b.text;
  kind_ = b.kind;
  blockIndex_ = index;
  line_ = 0;
  haveBlock_ = true;

  // DECIDED HERE, NOT IN drain(), because `prevKind_` is about to become this
  // block's own kind -- and drain() runs after that. Reading it there made every
  // paragraph its own predecessor, so a paragraph after a heading indented when the
  // whole rule is that it must not.
  indentThis_ = indentedAfter(prevKind_, b.kind, index == 0);
  const int myIndentF26 = indentThis_ ? indentF26_ : 0;
  // WordBreak::Anywhere, and body text is the one place it is right for a reason
  // the boards never had: NOTHING MAY LEAVE THE COLUMN. `Normal` lets a segment
  // wider than the column sit on its own line and overhang, which is fine on a
  // board whose copy the design chose and is not fine for a book -- `Le Fléau` has
  // a chanted phrase that ran 683px past a 492px column and off the panel.
  //
  // Breaking after a hyphen (see wrapProseLead) took that from eight lines in 96,823
  // to two, and both survivors separate their words with U+00A0: non-breaking by
  // definition, so a browser would overflow rather than break, which a panel cannot
  // do. `Anywhere` engages ONLY when a segment cannot fit a line at all -- which is
  // CSS's `overflow-wrap: break-word`, not a licence to break ordinary words.
  // THE SPANS ARE COPIED WITH THE TEXT, for the reason the text is copied: the wrap
  // measures against them and the caller may drop its block the moment this returns.
  // A HEADING IS SET IN CAPS, and it is transformed HERE rather than at draw time
  // because the wrap has to measure what will be drawn -- "Chapter I" and "CHAPTER
  // I" are not the same width, and measuring one to draw the other overflows the
  // column by the difference.
  if (b.kind == BlockKind::Heading) held_ = upperLatin1(held_);
  blockTracking_ = b.kind == BlockKind::Heading
                       ? Tracking::em(font_->ppem(), kHeadingTrackEm)
                       : m_.tracking;
  // The air above this block, in whole rows. Charged before its first line is laid.
  pendingBlankRows_ = blankRowsBefore(prevKind_, b.kind, index == 0);
  emphasis_ = b.emphasis;
  // A BLOCKQUOTE IS ITALIC AS A BLOCK, and it says so by being wholly emphasised
  // rather than by a second mechanism. That is the point: the wrap then MEASURES it
  // in the italic and the draw uses the same spans, so the two passes cannot
  // disagree -- where a "draw this kind italic" branch in the theme would be
  // measured roman and drawn italic, which is the 6%-to-9% error StyledFace exists
  // to remove.
  //
  // IT REPLACES the block's own spans rather than merging with them: `<em>` inside a
  // blockquote does not invert to roman. Fine typography does invert it; that is a
  // refinement, and the honest simple rule is that an emphasised phrase inside an
  // italic block is already italic.
  if (b.kind == BlockKind::Blockquote && !held_.empty())
    emphasis_ = {Span{0, static_cast<uint32_t>(held_.size())}};
  const StyledFace face{font_, m_.italic, &emphasis_};
  prose_ = wrapProseStyled(face, held_, columnWFor(b.kind), leadF26_, blockTracking_,
                           WordBreak::Anywhere, myIndentF26);
  prevKind_ = b.kind;
  if (!skipping_ && row_ == 0) pageStart_ = Cursor{blockIndex_, 0};
  drain();
}

void PageBuilder::drain() {
  if (!haveBlock_ || rows_ <= 0) return;
  const int count = prose_.lineCount();
  const int myIndentF26 = indentThis_ ? indentF26_ : 0;

  // THE BLANK ROWS ARE CHARGED BEFORE THE FIRST LINE, and while `skipping_` too --
  // a counting pass that skipped them would put a page boundary in a different place
  // from the drawing pass, which is the one thing pagination may never do.
  while (pendingBlankRows_ > 0 && row_ < rows_ && line_ < count) {
    --pendingBlankRows_;
    ++row_;
  }
  while (line_ < count && row_ < rows_) {
    // Everything before the requested start is measured and thrown away -- the
    // lines still have to be produced, because a page boundary depends on how many
    // came before it.
    if (skipping_) {
      if (blockIndex_ < skipTo_.block || (blockIndex_ == skipTo_.block && line_ < skipTo_.line)) {
        ++line_;
        continue;
      }
      skipping_ = false;
      row_ = 0;
      page_ = Page{};
      pageStart_ = Cursor{blockIndex_, line_};
    }

    if (!linesWanted_) {
      // Counting only: the wrap above already decided where this line ends, which is
      // all a page boundary needs.
      ++line_;
      ++row_;
      continue;
    }

    const std::string_view text = prose_.lines[static_cast<size_t>(line_)];
    // Only line 0 carries the indent, and only when the page did not resume
    // mid-paragraph: a page that begins at line 3 begins at the left margin.
    const int xIndentF26 = (line_ == 0) ? myIndentF26 : 0;
    LaidLine ln;
    ln.kind = kind_;
    ln.block = blockIndex_;
    ln.lastOfBlock = (line_ == count - 1);
    ln.firstOfBlock = (line_ == 0);
    // The marker goes out at the column's own edge, beside the first line only.
    if (kind_ == BlockKind::ListItem && ln.firstOfBlock) ln.markerX = m_.columnLeft;
    ln.tracking = blockTracking_;
    // A HEADING IS CENTRED ON ITS OWN MEASURED WIDTH, which is what `text-align:
    // center` does -- not on the column's centre with a half-width offset, and not on
    // the widest line's width. Measured with the SAME face and tracking the wrap
    // used, or a heading would centre off a width it was not laid out at.
    if (kind_ == BlockKind::Heading) {
      const StyledFace hface{font_, m_.italic, &emphasis_};
      const int w = hface.measure(text, 0, text.size(), blockTracking_);
      ln.x = columnLeftFor(kind_) + (columnWFor(kind_) - w) / 2;
    } else {
      ln.x = columnLeftFor(kind_) + f26ToPx(xIndentF26);
    }
    // One line box per row, its top accumulated in 1/64 px so the twentieth line
    // does not sit a pixel high off twenty roundings, and the baseline centred in
    // it by the same rule every other box on every screen uses.
    ln.baselineY = baselineInF26(*font_, pxToF26(m_.columnTop) + row_ * leadF26_, leadF26_);
    // THE LAST LINE OF A PARAGRAPH IS RAGGED. It is short by however much the
    // paragraph happened to end short, and stretching it to the margin is the
    // single most recognisable way justified text can be wrong.
    // JUSTIFIED TO THE BLOCK'S OWN COLUMN, not the page's. A blockquote inset 48px
    // each side that stretched its lines to `m_.columnW` would push them 96px past
    // its own right edge -- and it would look like justification is broken rather
    // than like the inset is.
    if (!ln.lastOfBlock && justifiable(kind_) && m_.justify)
      ln.extraPerGapF26 = stretchFor(*font_, text, columnWFor(kind_) - f26ToPx(xIndentF26),
                                     blockTracking_);
    ln.text.assign(text);
    // THE LINE'S SPANS, RE-BASED ONTO ITS OWN TEXT.
    //
    // The offset comes from the view's own pointer, and that is legitimate HERE and
    // almost nowhere else: `held_` is this object's copy of the block and
    // `prose_.lines` are views into exactly that buffer, both owned by this builder
    // for the whole of this loop. The technique is otherwise banned in this
    // codebase -- two tests once recovered a line's block by comparing
    // `text.data()` pointers and broke the moment a Page owned its text -- so the
    // guard is asserted rather than assumed.
    if (!emphasis_.empty()) {
      const size_t off = static_cast<size_t>(text.data() - held_.data());
      if (off <= held_.size() && off + text.size() <= held_.size())
        ln.emphasis = clipTo(emphasis_, off, off + text.size());
    }
    page_.lines.push_back(std::move(ln));

    ++line_;
    ++row_;
  }

  if (line_ >= count) {
    // The block is spent; its text can go, and with it the views into it -- and its
    // spans, which index that text and mean nothing without it.
    haveBlock_ = false;
    held_.clear();
    emphasis_.clear();
    prose_.lines.clear();
  }
}

bool PageBuilder::ready() const { return rows_ > 0 && row_ >= rows_; }

Page PageBuilder::take() {
  Page out = std::move(page_);
  // `next` names where the following page begins: still inside this block if lines
  // remain, otherwise the start of the next block.
  out.next = haveBlock_ ? Cursor{blockIndex_, line_} : Cursor{blockIndex_ + 1, 0};
  out.lastPage = false;
  beginPage();
  drain();
  return out;
}

Page PageBuilder::finish() {
  Page out = std::move(page_);
  out.next = haveBlock_ ? Cursor{blockIndex_, line_} : Cursor{blockIndex_ + 1, 0};
  out.lastPage = true;
  page_ = Page{};
  row_ = 0;
  return out;
}

Page layoutPage(const Document& doc, const GlyphSource& font, const PageMetrics& m, Cursor from) {
  // ONE SET OF LAYOUT RULES. This used to be its own loop over the Document, which
  // would now be a second copy of every decision PageBuilder makes -- the indent,
  // the ragged last line, the fractional line box. So it feeds the builder instead
  // and takes the page that begins where it was asked to.
  const int blocks = static_cast<int>(doc.blocks.size());
  if (from.block >= blocks) {
    Page p;
    p.next = from;
    p.lastPage = true;
    return p;
  }

  PageBuilder pb(font, m);
  // A column too short for one line box: an EMPTY page whose `next` equals `from`,
  // and NOT a last page. layout.h states that contract and a caller must not loop
  // on it; reporting lastPage here would tell a caller the chapter had ended.
  if (!pb.viable()) {
    Page p;
    p.next = from;
    return p;
  }
  pb.startAt(from);
  for (int b = 0; b < blocks; ++b) {
    pb.add(doc.blocks[static_cast<size_t>(b)], b);
    if (pb.ready()) {
      Page p = pb.take();
      // `lastPage` is the builder's only unknowable: it fills a page without
      // knowing whether another block follows. Here the Document says.
      p.lastPage = p.next.block >= blocks;
      return p;
    }
  }
  return pb.finish();
}

}  // namespace reader
