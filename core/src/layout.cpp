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

}  // namespace

PageBuilder::PageBuilder(const GlyphSource& font, const PageMetrics& m)
    : font_(&font), m_(m) {
  leadF26_ = Tracking::em(font.ppem(), m.leadEm1000).f26();
  indentF26_ = Tracking::em(font.ppem(), m.indentEm1000).f26();
  // A column that cannot hold one line box yields no pages at all, rather than
  // dividing by zero -- layout.h says a caller must not loop on that.
  rows_ = (leadF26_ > 0 && m.columnW > 0) ? pxToF26(m.columnH) / leadF26_ : 0;
  beginPage();
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
  prose_ = wrapProseLead(*font_, held_, m_.columnW, leadF26_, m_.tracking, WordBreak::Anywhere,
                         myIndentF26);
  prevKind_ = b.kind;
  if (!skipping_ && row_ == 0) pageStart_ = Cursor{blockIndex_, 0};
  drain();
}

void PageBuilder::drain() {
  if (!haveBlock_ || rows_ <= 0) return;
  const int count = prose_.lineCount();
  const int myIndentF26 = indentThis_ ? indentF26_ : 0;

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
    ln.x = m_.columnLeft + f26ToPx(xIndentF26);
    // One line box per row, its top accumulated in 1/64 px so the twentieth line
    // does not sit a pixel high off twenty roundings, and the baseline centred in
    // it by the same rule every other box on every screen uses.
    ln.baselineY = baselineInF26(*font_, pxToF26(m_.columnTop) + row_ * leadF26_, leadF26_);
    // THE LAST LINE OF A PARAGRAPH IS RAGGED. It is short by however much the
    // paragraph happened to end short, and stretching it to the margin is the
    // single most recognisable way justified text can be wrong.
    if (!ln.lastOfBlock && justifiable(kind_))
      ln.extraPerGapF26 =
          stretchFor(*font_, text, m_.columnW - f26ToPx(xIndentF26), m_.tracking);
    ln.text.assign(text);
    page_.lines.push_back(std::move(ln));

    ++line_;
    ++row_;
  }

  if (line_ >= count) {
    // The block is spent; its text can go, and with it the views into it.
    haveBlock_ = false;
    held_.clear();
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
