// Layout: a chapter into pages, and the assertion that measuring one does not
// rasterise it.
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "reader/document.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/text.h"

namespace {

using reader::BlockKind;
using reader::Cursor;
using reader::Document;
using reader::LaidLine;
using reader::Page;
using reader::PageMetrics;
using reader::ScalableFont;

// The board's own face and size: design/Reader.dc.html's `font-size: 32px`.
struct Body {
  std::vector<uint8_t> bytes = golden::slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
  ScalableFont face;
  explicit Body(int sizePx = reader::kBodyPpem) {
    REQUIRE(face.init(bytes.data(), bytes.size(), sizePx));
    REQUIRE(face.ready());
  }
};

// The board's column: 480px wide less 18px of side padding each way.
PageMetrics boardMetrics(int columnH = 500) {
  PageMetrics m;
  m.columnLeft = 18;
  m.columnTop = 100;
  m.columnW = 444;
  m.columnH = columnH;
  return m;
}

Document docOf(std::initializer_list<const char*> paras) {
  Document d;
  for (const char* p : paras) d.blocks.push_back({BlockKind::Paragraph, p});
  return d;
}

// A paragraph long enough to wrap over many lines at 32px in a 444px column.
std::string longPara(const char* lead, int sentences) {
  std::string s = lead;
  for (int i = 0; i < sentences; ++i)
    s += " And then a further sentence of quite ordinary words, numbered " +
         std::to_string(i) + ", to give the wrap something to chew on.";
  return s;
}

int gapsIn(std::string_view s) {
  int n = 0;
  for (const char c : s)
    if (c == ' ') ++n;
  return n;
}

}  // namespace

TEST_CASE("A LAYOUT PASS RASTERISES NOTHING") {
  // Design decision 3 of Phase 3A, and this is where it earns itself: measuring a
  // chapter must not cost what drawing it costs. Asserted on the cache's own
  // counter rather than on the discipline of not calling glyph().
  Body b;
  Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument(
      "<body><h1>One</h1><p>" + longPara("Miss Brooke had that kind of beauty.", 12) +
          "</p><p>" + longPara("Her sister Celia wore a necklace.", 12) + "</p></body>",
      d, &why));

  const ScalableFont::CacheStats before = b.face.cacheStats();
  CHECK(before.rasterisations == 0);

  int pages = 0;
  Cursor at{};
  for (;;) {
    const Page p = reader::layoutPage(d, b.face, boardMetrics(), at);
    ++pages;
    REQUIRE(pages < 100);  // a layout that does not advance is the bug this catches
    if (p.lastPage) break;
    REQUIRE(p.next != at);
    at = p.next;
  }
  CHECK(pages > 1);  // it really did paginate

  const ScalableFont::CacheStats after = b.face.cacheStats();
  CHECK(after.rasterisations == 0);
}

TEST_CASE("a page fills its column and no more") {
  Body b;
  const Document d = docOf({longPara("A paragraph.", 40).c_str()});
  const PageMetrics m = boardMetrics(500);
  const Page p = reader::layoutPage(d, b.face, m, Cursor{});

  const int leadF26 = reader::Tracking::em(b.face.ppem(), reader::kBodyLeadEm).f26();
  const int rows = reader::pxToF26(m.columnH) / leadF26;
  CHECK(p.lines.size() == static_cast<size_t>(rows));
  // Every baseline inside the column.
  for (const LaidLine& ln : p.lines) {
    CHECK(ln.baselineY > m.columnTop);
    CHECK(ln.baselineY <= m.columnTop + m.columnH);
  }
}

TEST_CASE("baselines advance by the line box, with no accumulated rounding") {
  Body b;
  const Document d = docOf({longPara("A paragraph.", 40).c_str()});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(), Cursor{});
  REQUIRE(p.lines.size() > 4);
  // 1.7 x 32px is 54.4px, so the gaps must ALTERNATE 54 and 55 rather than all
  // being 54 -- that alternation is the fractional accumulation working. All-54
  // would put the twentieth line 8px high.
  bool sawShort = false, sawLong = false;
  for (size_t i = 1; i < p.lines.size(); ++i) {
    const int step = p.lines[i].baselineY - p.lines[i - 1].baselineY;
    CHECK(step >= 54);
    CHECK(step <= 55);
    if (step == 54) sawShort = true;
    if (step == 55) sawLong = true;
  }
  CHECK(sawShort);
  CHECK(sawLong);
}

TEST_CASE("A PAGE BOUNDARY MAY LAND INSIDE A PARAGRAPH") {
  Body b;
  // One paragraph, far longer than a page. If a paragraph were treated as
  // unbreakable this would lay out as one over-long page or as nothing.
  const Document d = docOf({longPara("A single enormous paragraph.", 60).c_str()});
  const PageMetrics m = boardMetrics(300);
  const Page first = reader::layoutPage(d, b.face, m, Cursor{});
  REQUIRE_FALSE(first.lastPage);
  CHECK(first.next.block == 0);  // still inside block 0
  CHECK(first.next.line == static_cast<int>(first.lines.size()));

  const Page second = reader::layoutPage(d, b.face, m, first.next);
  REQUIRE(!second.lines.empty());
  // The second page resumes exactly where the first stopped -- no line repeated
  // and none skipped, which is the off-by-one this case exists to find.
  CHECK(second.lines.front().text.data() ==
        first.lines.back().text.data() + first.lines.back().text.size() + 1);
}

TEST_CASE("every line of the chapter appears exactly once across the pages") {
  Body b;
  Document d = docOf({longPara("First.", 10).c_str(), longPara("Second.", 10).c_str(),
                      longPara("Third.", 10).c_str()});
  std::vector<std::string> seen;
  Cursor at{};
  for (int guard = 0; guard < 200; ++guard) {
    const Page p = reader::layoutPage(d, b.face, boardMetrics(280), at);
    for (const LaidLine& ln : p.lines) seen.push_back(std::string(ln.text));
    if (p.lastPage) break;
    at = p.next;
  }
  // Rejoining the lines of each block must reproduce the block, spaces and all --
  // the wrap eats exactly one space at each break.
  std::string joined;
  for (const std::string& s : seen) {
    if (!joined.empty()) joined += ' ';
    joined += s;
  }
  std::string all;
  for (const reader::Block& blk : d.blocks) {
    if (!all.empty()) all += ' ';
    all += blk.text;
  }
  CHECK(joined == all);
}

// --- Justification -----------------------------------------------------------

TEST_CASE("a justified line reaches the right margin, and the last line does not") {
  Body b;
  // Tall enough that the paragraph ENDS on this page -- otherwise the page's last
  // line is a mid-paragraph line and has every right to be justified.
  const Document d = docOf({longPara("A paragraph to justify.", 8).c_str()});
  const PageMetrics m = boardMetrics(2000);
  const Page p = reader::layoutPage(d, b.face, m, Cursor{});
  REQUIRE(p.lines.size() > 3);

  reader::Framebuffer fb(480, 2400);
  for (size_t i = 0; i + 1 < p.lines.size(); ++i) {
    const LaidLine& ln = p.lines[i];
    if (ln.extraPerGapF26 == 0) continue;  // ragged by the stretch cap
    const int drawn = reader::drawTextJustified(fb, b.face, ln.x, ln.baselineY, ln.text,
                                               ln.extraPerGapF26);
    // Within a pixel of the column's right edge: the stretch is integer 1/64ths,
    // so the remainder of the division is lost -- at most one 1/64 per gap.
    const int want = m.columnW - (ln.x - m.columnLeft);
    CAPTURE(i);
    CAPTURE(drawn);
    CAPTURE(want);
    CHECK(drawn <= want);
    CHECK(drawn >= want - 2);
  }
  // THE LAST LINE IS RAGGED. Not "usually shorter" -- it carries no stretch at
  // all, which is what makes it ragged rather than justified-and-lucky.
  CHECK(p.lines.back().extraPerGapF26 == 0);
}

TEST_CASE("the last line of EVERY paragraph is ragged, not just the page's") {
  Body b;
  Document d = docOf({longPara("First paragraph.", 4).c_str(),
                      longPara("Second paragraph.", 4).c_str()});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(2400), Cursor{});
  REQUIRE(p.lastPage);
  // Find each block's final line by watching the text pointer leave the block.
  for (size_t i = 0; i < p.lines.size(); ++i) {
    const std::string_view t = p.lines[i].text;
    const bool blockEnds =
        (i + 1 == p.lines.size()) ||
        (p.lines[i + 1].text.data() > t.data() + t.size() + 1);
    if (blockEnds) CHECK(p.lines[i].extraPerGapF26 == 0);
  }
}

TEST_CASE("a line with no gaps is never stretched") {
  Body b;
  // A single unbreakable word wider than the column: the wrap gives it its own
  // line, and there is nothing on that line to distribute slack across.
  Document d;
  d.blocks.push_back({BlockKind::Paragraph,
                      "Short line here pneumonoultramicroscopicsilicovolcanoconiosis and after."});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(), Cursor{});
  for (const LaidLine& ln : p.lines)
    if (gapsIn(ln.text) == 0) CHECK(ln.extraPerGapF26 == 0);
}

TEST_CASE("A LINE IS SET RAGGED RATHER THAN OPENING A CORRIDOR") {
  // kMinJustifyFillPercent's reason, on the actual string from tools/mkepub.py: the line
  // before an unbreakably long word holds few words and most of the column as
  // slack, and justifying it puts two words at opposite margins.
  Body b;
  Document d;
  d.blocks.push_back(
      {BlockKind::Paragraph,
       "Two words pneumonoultramicroscopicsilicovolcanoconiosis then several more "
       "words to make a second line and a third so the paragraph is not one line."});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(), Cursor{});
  REQUIRE(p.lines.size() >= 2);
  // The first line is "Two words" -- one gap, ~330px of slack. Ragged.
  CHECK(p.lines[0].extraPerGapF26 == 0);
}

TEST_CASE("AN ORDINARY LINE IS JUSTIFIED EVEN WHEN ITS SLACK FALLS ACROSS FEW GAPS") {
  // The regression a per-gap stretch cap caused, on design/Reader.dc.html's own
  // copy. "necklace, and the two of" is 367px of text in a 444px column -- 83%
  // full, unremarkable prose -- but its 77px of slack falls across only four gaps,
  // so a cap of three space-widths refused it by one pixel and set it ragged
  // directly beneath a line it had justified. Justification is a property of the
  // LINE's fill, not of the gap count; this is that pinned.
  Body b;
  Document d = docOf({"Short opening.",
                      "Her sister Celia wore a necklace, and the two of them had that air of "
                      "being dressed alike which is never quite an accident."});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(2400), Cursor{});
  REQUIRE(p.lastPage);
  int raggedMidParagraph = 0;
  for (size_t i = 0; i < p.lines.size(); ++i) {
    const std::string_view t = p.lines[i].text;
    const bool blockEnds = (i + 1 == p.lines.size()) ||
                           (p.lines[i + 1].text.data() > t.data() + t.size() + 1);
    if (blockEnds || gapsIn(t) == 0) continue;
    const int avail = 444 - (p.lines[i].x - 18);
    const int nat = b.face.measure(t);
    if (nat * 100 >= avail * reader::kMinJustifyFillPercent &&
        p.lines[i].extraPerGapF26 == 0) {
      ++raggedMidParagraph;
      CAPTURE(std::string(t));
      CAPTURE(nat);
      CAPTURE(avail);
    }
  }
  CHECK(raggedMidParagraph == 0);
}

// --- The indent --------------------------------------------------------------

TEST_CASE("A CONTINUING PARAGRAPH IS INDENTED AND THE FIRST IS NOT") {
  Body b;
  Document d = docOf({longPara("First.", 3).c_str(), longPara("Second.", 3).c_str()});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(2400), Cursor{});
  REQUIRE(p.lastPage);
  const int indent = reader::f26ToPx(reader::Tracking::em(b.face.ppem(), reader::kBodyIndentEm).f26());
  CHECK(indent == 48);  // 1.5em at 32px, the board's number

  CHECK(p.lines.front().x == 18);  // flush: nothing above it to continue
  // The second paragraph's first line is the one that jumps in.
  int indented = 0;
  for (const LaidLine& ln : p.lines)
    if (ln.x == 18 + indent) ++indented;
  CHECK(indented == 1);
}

TEST_CASE("a paragraph after a heading is flush, because the heading is the break") {
  Body b;
  Document d;
  d.blocks.push_back({BlockKind::Heading, "One: The Rope Ferry"});
  d.blocks.push_back({BlockKind::Paragraph, longPara("The ferry was a rope.", 3)});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(2400), Cursor{});
  for (const LaidLine& ln : p.lines) CHECK(ln.x == 18);
}

TEST_CASE("an indented line is wrapped to the narrower column it is drawn in") {
  // The failure this catches is the one that looks fine in the model and overflows
  // on glass: wrapping at the full column and then drawing 48px in.
  Body b;
  Document d = docOf({"Short.", longPara("A continuing paragraph that must wrap.", 6).c_str()});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(2400), Cursor{});
  reader::Framebuffer fb(480, 2600);
  for (const LaidLine& ln : p.lines) {
    const int w = reader::drawTextJustified(fb, b.face, ln.x, ln.baselineY, ln.text,
                                            ln.extraPerGapF26);
    CAPTURE(std::string(ln.text));
    CHECK(ln.x + w <= 18 + 444);
  }
}

TEST_CASE("a page resuming mid-paragraph starts flush, not indented again") {
  Body b;
  Document d = docOf({"Short.", longPara("A continuing paragraph.", 40).c_str()});
  const PageMetrics m = boardMetrics(300);
  const Page first = reader::layoutPage(d, b.face, m, Cursor{});
  REQUIRE_FALSE(first.lastPage);
  const Page second = reader::layoutPage(d, b.face, m, first.next);
  REQUIRE(!second.lines.empty());
  CHECK(second.lines.front().x == 18);
}

// --- Boundaries --------------------------------------------------------------

TEST_CASE("an empty document is one last page with no lines") {
  Body b;
  const Document d;
  const Page p = reader::layoutPage(d, b.face, boardMetrics(), Cursor{});
  CHECK(p.lines.empty());
  CHECK(p.lastPage);
}

TEST_CASE("a cursor past the end is a last page, not a read past the vector") {
  Body b;
  const Document d = docOf({"Only block."});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(), Cursor{7, 0});
  CHECK(p.lines.empty());
  CHECK(p.lastPage);
}

TEST_CASE("a column too short for one line yields an empty page that does not advance") {
  // Documented in layout.h: the caller must not loop on this. Pinned so the
  // contract is checkable rather than only stated.
  Body b;
  const Document d = docOf({"A block."});
  const Page p = reader::layoutPage(d, b.face, boardMetrics(10), Cursor{});
  CHECK(p.lines.empty());
  CHECK(p.next == Cursor{});
  CHECK_FALSE(p.lastPage);
}

TEST_CASE("a real chapter paginates, and the page count is what a 32px face gives") {
  Body b;
  const std::string_view chapter =
      "<body><h1>Two</h1>"
      "<p class=\"first\">There were scales in the back room, and a set of brass "
      "weights in a felt-lined case, and the felt had gone the colour of weak tea. "
      "Every weight was stamped with a number it no longer deserved.</p>"
      "<p>Now a word with no convenient break in it, for the hyphenation patterns to "
      "argue with: antidisestablishmentarianism. And another, longer, which no line "
      "will hold: pneumonoultramicroscopicsilicovolcanoconiosis.</p>"
      "<blockquote>A blockquote, indented and italic, so the engine has to change two "
      "things at once and put them back afterwards.</blockquote></body>";
  Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument(chapter, d, &why));

  int pages = 0, lines = 0;
  Cursor at{};
  for (;;) {
    const Page p = reader::layoutPage(d, b.face, boardMetrics(520), at);
    ++pages;
    lines += static_cast<int>(p.lines.size());
    REQUIRE(pages < 50);
    if (p.lastPage) break;
    at = p.next;
  }
  CAPTURE(pages);
  CAPTURE(lines);
  CHECK(pages >= 2);
  CHECK(lines > 10);
}
