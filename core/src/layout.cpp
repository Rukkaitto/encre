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
bool indented(const Document& doc, int at) {
  if (at <= 0) return false;
  const size_t i = static_cast<size_t>(at);
  return doc.blocks[i].kind == BlockKind::Paragraph &&
         doc.blocks[i - 1].kind == BlockKind::Paragraph;
}

// Whether this kind's lines are justified at all.
//
// Paragraphs and quotes are body text and the board says `text-align: justify`.
// Headings and list items are NOT: a heading is a display line whose slack is
// meant to be visible, and a list item is usually short enough that justifying it
// opens the corridor kMaxGapStretch exists to prevent -- on every line rather than
// occasionally.
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
  const int perGapF26 = pxToF26(availW - naturalW) / gaps;

  // The ragged fallback. See kMaxGapStretch: the alternative on the line before
  // an unbreakably long word is two words at opposite margins.
  const std::optional<int> spaceAdv = font.advance(U' ');
  const int spaceF26 = pxToF26(spaceAdv.value_or(font.ppem() / 4));
  if (spaceF26 > 0 && perGapF26 > kMaxGapStretch * spaceF26) return 0;
  return perGapF26;
}

}  // namespace

Page layoutPage(const Document& doc, const GlyphSource& font, const PageMetrics& m, Cursor from) {
  Page page;
  page.next = from;

  const int leadF26 = Tracking::em(font.ppem(), m.leadEm1000).f26();
  const int indentF26 = Tracking::em(font.ppem(), m.indentEm1000).f26();
  const int blocks = static_cast<int>(doc.blocks.size());
  if (from.block >= blocks) {
    page.lastPage = true;
    return page;
  }
  // A column that cannot hold one line box yields an EMPTY page whose `next`
  // equals `from`. The caller must not loop on that -- see layout.h.
  if (leadF26 <= 0 || m.columnW <= 0) return page;
  const int rows = pxToF26(m.columnH) / leadF26;
  if (rows <= 0) return page;

  int row = 0;
  for (int b = from.block; b < blocks && row < rows; ++b) {
    const Block& blk = doc.blocks[static_cast<size_t>(b)];
    const int myIndentF26 = indented(doc, b) ? indentF26 : 0;
    // RE-WRAPPING, not caching. This is the paragraph the previous page ended in
    // the middle of, and wrapping it again to reach line `from.line` costs
    // advances only -- which is what makes one-page-at-a-time affordable. See
    // layout.h.
    const Prose wrapped = wrapProseLead(font, blk.text, m.columnW, leadF26, m.tracking,
                                        WordBreak::Normal, myIndentF26);

    const int first = (b == from.block) ? from.line : 0;
    const int count = wrapped.lineCount();
    for (int l = first; l < count && row < rows; ++l, ++row) {
      const std::string_view text = wrapped.lines[static_cast<size_t>(l)];
      // Only line 0 carries the indent, and only when the page did not resume
      // mid-paragraph: a page that begins at line 3 begins at the left margin.
      const int xIndentF26 = (l == 0) ? myIndentF26 : 0;
      LaidLine ln;
      ln.kind = blk.kind;
      ln.x = m.columnLeft + f26ToPx(xIndentF26);
      // One line box per row, its top accumulated in 1/64 px so the twentieth
      // line does not sit a pixel high off twenty roundings, and the baseline
      // centred in it by the same rule every other box on every screen uses.
      ln.baselineY = baselineInF26(font, pxToF26(m.columnTop) + row * leadF26, leadF26);
      // THE LAST LINE OF A PARAGRAPH IS RAGGED. It is short by however much the
      // paragraph happened to end short, and stretching it to the margin is the
      // single most recognisable way justified text can be wrong.
      const bool last = (l == count - 1);
      if (!last && justifiable(blk.kind))
        ln.extraPerGapF26 =
            stretchFor(font, text, m.columnW - f26ToPx(xIndentF26), m.tracking);
      ln.text = text;
      page.lines.push_back(ln);

      page.next = (l + 1 < count) ? Cursor{b, l + 1} : Cursor{b + 1, 0};
    }
  }

  page.lastPage = page.next.block >= blocks;
  return page;
}

}  // namespace reader
