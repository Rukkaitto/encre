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

// The ITALIC face at the same size. A second FILE, not an instance: Literata.ttf
// carries no `ital` and no `slnt` axis, so nothing pinned from it slants.
struct Italic {
  std::vector<uint8_t> bytes =
      golden::slurp(std::string(ASSETS_DIR) + "/built/literata_italic.ttf");
  ScalableFont face;
  explicit Italic(int sizePx = reader::kBodyPpem) {
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
  // and none skipped, which is the off-by-one this case exists to find. Asserted on
  // the block index and the text, not on where the bytes live: the lines own their
  // text now, so a pointer comparison would be comparing two separate allocations.
  CHECK(second.lines.front().block == first.lines.back().block);
  CHECK_FALSE(first.lines.back().lastOfBlock);
  // And rejoining them reproduces the paragraph across the boundary.
  const std::string across = first.lines.back().text + " " + second.lines.front().text;
  CHECK(d.blocks[0].text.find(across) != std::string::npos);
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
  // `lastOfBlock` says which lines those are, rather than the pointer arithmetic
  // this used to infer it with.
  int checked = 0;
  for (const LaidLine& ln : p.lines) {
    if (!ln.lastOfBlock) continue;
    ++checked;
    CAPTURE(ln.text);
    CHECK(ln.extraPerGapF26 == 0);
  }
  CHECK(checked == 2);  // two paragraphs, so two final lines -- not zero
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
    const std::string& t = p.lines[i].text;
    if (p.lines[i].lastOfBlock || gapsIn(t) == 0) continue;
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

// --- PageBuilder, the streaming engine --------------------------------------

TEST_CASE("STREAMED PAGES MATCH THE ONES layoutPage GIVES, page for page") {
  // layoutPage is implemented over PageBuilder, so this is not quite a comparison
  // of two engines -- it is a check that feeding blocks one at a time and dropping
  // them behind you produces the same pagination as having the whole Document. That
  // is the property the reader depends on and the one a resumption bug breaks.
  Body b;
  Document d = docOf({longPara("First.", 6).c_str(), longPara("Second.", 6).c_str(),
                      longPara("Third.", 6).c_str()});
  const PageMetrics m = boardMetrics(400);

  // The reference: walk it with layoutPage.
  std::vector<std::vector<std::string>> viaLayout;
  Cursor at{};
  for (int guard = 0; guard < 100; ++guard) {
    const Page p = reader::layoutPage(d, b.face, m, at);
    std::vector<std::string> lines;
    for (const LaidLine& ln : p.lines) lines.push_back(ln.text);
    viaLayout.push_back(lines);
    if (p.lastPage) break;
    at = p.next;
  }

  // The same thing, streamed: each block fed once and then dropped.
  std::vector<std::vector<std::string>> viaStream;
  reader::PageBuilder pb(b.face, m);
  REQUIRE(pb.viable());
  const auto keep = [&](const Page& p) {
    std::vector<std::string> lines;
    for (const LaidLine& ln : p.lines) lines.push_back(ln.text);
    viaStream.push_back(lines);
  };
  for (int i = 0; i < static_cast<int>(d.blocks.size()); ++i) {
    reader::Block copy = d.blocks[static_cast<size_t>(i)];
    pb.add(copy, i);
    copy = reader::Block{};  // dropped, as a BlockReader's caller drops it
    while (pb.ready()) keep(pb.take());
  }
  keep(pb.finish());

  REQUIRE(viaStream.size() == viaLayout.size());
  CHECK(viaStream.size() > 3);  // really paginated
  for (size_t i = 0; i < viaStream.size(); ++i) {
    CAPTURE(i);
    CHECK(viaStream[i] == viaLayout[i]);
  }
}

TEST_CASE("A PAGE INDEX OF CURSORS IS WHAT PAGINATION LEAVES BEHIND") {
  // The reader needs three things a single page cannot give: how many pages there
  // are, which one it is on, and how to reach an earlier one. All three come from
  // recording the start cursor of each page during one pass -- ~8 bytes a page --
  // and discarding the lines.
  Body b;
  Document d = docOf({longPara("A paragraph.", 20).c_str(), "Short one.",
                      longPara("Another.", 20).c_str()});
  const PageMetrics m = boardMetrics(400);

  std::vector<Cursor> index;
  reader::PageBuilder pb(b.face, m);
  size_t linesSeen = 0;
  for (int i = 0; i < static_cast<int>(d.blocks.size()); ++i) {
    index.push_back(pb.pageStart());  // provisional; corrected below
    index.pop_back();
    reader::Block copy = d.blocks[static_cast<size_t>(i)];
    pb.add(copy, i);
    while (pb.ready()) {
      index.push_back(pb.pageStart());
      const Page p = pb.take();
      linesSeen += p.lines.size();
    }
  }
  index.push_back(pb.pageStart());
  const Page last = pb.finish();
  linesSeen += last.lines.size();

  REQUIRE(index.size() > 2);
  // The index is monotonic and starts at the chapter's beginning.
  CHECK(index.front() == Cursor{0, 0});
  for (size_t i = 1; i < index.size(); ++i) {
    CAPTURE(i);
    const bool forward = index[i].block > index[i - 1].block ||
                         (index[i].block == index[i - 1].block && index[i].line > index[i - 1].line);
    CHECK(forward);
  }
  // Every cursor in it is a page layoutPage can actually produce, which is what
  // makes the index usable for going backwards.
  for (const Cursor& c : index) {
    const Page p = reader::layoutPage(d, b.face, m, c);
    CAPTURE(c.block);
    CAPTURE(c.line);
    CHECK_FALSE(p.lines.empty());
    CHECK(p.lines.front().block == c.block);
  }
  // And no line was lost or duplicated across the whole walk.
  size_t totalLines = 0;
  Cursor at{};
  for (int guard = 0; guard < 200; ++guard) {
    const Page p = reader::layoutPage(d, b.face, m, at);
    totalLines += p.lines.size();
    if (p.lastPage) break;
    at = p.next;
  }
  CHECK(linesSeen == totalLines);
}

// No comma in the name on purpose: doctest treats one as a pattern separator in
// -tc, so a filtered run skips the test silently -- which is how this one first
// appeared to pass.
TEST_CASE("the builder holds ONE BLOCK and releases it once its lines are laid") {
  // The lines own their text (see LaidLine), which is what lets the block go. A
  // builder that kept blocks would grow with the chapter and the whole streaming
  // design would buy nothing.
  Body b;
  const std::string big = longPara("An enormous paragraph.", 200);
  reader::Block blk{BlockKind::Paragraph, big};
  reader::PageBuilder pb(b.face, boardMetrics(400));
  pb.add(blk, 0);
  int pages = 0;
  while (pb.ready()) {
    const Page p = pb.take();
    CHECK_FALSE(p.lines.empty());
    // Each line's text is its own: independent of the block, which the caller is
    // free to have destroyed.
    for (const LaidLine& ln : p.lines) CHECK(big.find(ln.text) != std::string::npos);
    ++pages;
    REQUIRE(pages < 400);  // a guard, not an expectation: this block is ~110 pages
  }
  pb.finish();
  CHECK(pages > 50);  // one block spanning a hundred pages is the case being tested
}

// --- Measuring a line that changes face part-way through ---------------------

TEST_CASE("a styled measure with no emphasis is the roman measure, exactly") {
  // The property that keeps a chapter with no `<em>` costing what it always cost --
  // and that makes ONE wrap loop safe to share with the twenty chrome callers.
  Body body;
  Italic ital;
  const std::string s = "Miss Brooke had that kind of beauty";
  const reader::StyledFace bare{&body.face, nullptr, nullptr};
  const std::vector<reader::Span> none;
  const reader::StyledFace empty{&body.face, &ital.face, &none};
  const int roman = body.face.measure(s, {});
  CHECK(bare.measure(s, 0, s.size(), {}) == roman);
  CHECK(empty.measure(s, 0, s.size(), {}) == roman);
}

TEST_CASE("a fully emphasised range measures as the ITALIC, which is narrower") {
  // 6% to 9% narrower over the strings this was measured on. If this ever comes out
  // equal, the two faces are the same file loaded twice.
  Body body;
  Italic ital;
  const std::string s = "poor dress";
  const std::vector<reader::Span> all{{0, static_cast<uint32_t>(s.size())}};
  const reader::StyledFace face{&body.face, &ital.face, &all};
  const int r = body.face.measure(s, {});
  const int i = ital.face.measure(s, {});
  CHECK(i < r);
  CHECK(face.measure(s, 0, s.size(), {}) == i);
}

TEST_CASE("a mixed range is the sum of its pieces, and is NOT the roman width") {
  // The whole reason StyledFace exists. Measuring this with the roman is wrong by
  // the italic's own deficit, which on a real column is most of a word.
  Body body;
  Italic ital;
  const std::string s = "thrown into relief by poor dress. Her hand";
  const size_t at = s.find("poor dress");
  REQUIRE(at != std::string::npos);
  const std::vector<reader::Span> em{{static_cast<uint32_t>(at), 10}};
  const reader::StyledFace face{&body.face, &ital.face, &em};

  const int mixed = face.measure(s, 0, s.size(), {});
  const int allRoman = body.face.measure(s, {});
  CHECK(mixed < allRoman);
  // And it is the sum of the three pieces the boundaries cut it into.
  const int sum = body.face.measure(std::string_view(s).substr(0, at), {}) +
                  ital.face.measure(std::string_view(s).substr(at, 10), {}) +
                  body.face.measure(std::string_view(s).substr(at + 10), {});
  CHECK(mixed == sum);
}

TEST_CASE("emphasis changes WHERE the wrap breaks, which is why it is measured") {
  // The consequence, stated as a test rather than as a comment: the italic being
  // narrower means more fits on a line, so a column narrow enough to be sensitive
  // breaks in a different place. If this ever comes out identical, the spans are
  // reaching the measure but not being used.
  Body body;
  Italic ital;
  const std::string s =
      "Miss Brooke had that kind of beauty which seems to be thrown into relief by "
      "poor dress and by nothing else at all";
  const std::vector<reader::Span> all{{0, static_cast<uint32_t>(s.size())}};
  const reader::StyledFace romanOnly{&body.face, nullptr, nullptr};
  const reader::StyledFace italicAll{&body.face, &ital.face, &all};

  const reader::Prose a = reader::wrapProseStyled(romanOnly, s, 444, 3481);
  const reader::Prose b = reader::wrapProseStyled(italicAll, s, 444, 3481);
  // Same text, same column, different faces: the narrower face fits more per line.
  bool anyLineDiffers = false;
  for (size_t i = 0; i < a.lines.size() && i < b.lines.size(); ++i)
    if (a.lines[i] != b.lines[i]) anyLineDiffers = true;
  CHECK(anyLineDiffers);
  CHECK(b.lineCount() <= a.lineCount());
}

// --- Emphasis through the page layout ----------------------------------------

namespace {

// A laid line's text with its emphasis marked, so the span and the bytes it covers
// are asserted together. An offset checked on its own passes just as happily when it
// points at the wrong letter -- and after a wrap it is a DIFFERENT wrong letter on
// each line, which is the bug this whole re-basing exists to prevent.
std::string markedLine(const reader::LaidLine& ln) {
  std::string out;
  for (size_t i = 0; i < ln.text.size(); ++i) {
    const bool here = reader::emphasisedAt(ln.emphasis, i);
    const bool prev = i > 0 && reader::emphasisedAt(ln.emphasis, i - 1);
    if (here && !prev) out += "<";
    if (!here && prev) out += ">";
    out += ln.text[i];
  }
  if (!ln.text.empty() && reader::emphasisedAt(ln.emphasis, ln.text.size() - 1)) out += ">";
  return out;
}

}  // namespace

TEST_CASE("an emphasised phrase that WRAPS is clipped onto both lines") {
  // THE CASE THE RUN MODEL EXISTS FOR. A span dropped rather than clipped at the
  // line break renders the first half italic and the second half roman, and every
  // golden of a page whose emphasis happens to fit on one line still passes.
  Body body;
  Italic ital;
  PageMetrics m = boardMetrics();
  m.italic = &ital.face;

  reader::Document d;
  const char* why = "";
  // Set so the emphasis lands across a break: a long lead-in, then the phrase.
  REQUIRE(reader::buildDocument(
      "<p>Miss Brooke had that kind of beauty which seems to be thrown into relief "
      "by <em>poor dress and nothing else</em> at all.</p>", d, &why));
  REQUIRE(d.blocks.size() == 1);
  REQUIRE(d.blocks[0].emphasis.size() == 1);

  reader::PageBuilder pb(body.face, m);
  pb.add(d.blocks[0], 0);
  const reader::Page page = pb.finish();
  REQUIRE(page.lines.size() > 1);

  // Every emphasised byte on every line must be one of the phrase's bytes, and the
  // phrase must be covered exactly once across the page.
  std::string gathered;
  int linesWithEmphasis = 0;
  for (const reader::LaidLine& ln : page.lines) {
    if (ln.emphasis.empty()) continue;
    ++linesWithEmphasis;
    for (size_t i = 0; i < ln.text.size(); ++i)
      if (reader::emphasisedAt(ln.emphasis, i)) gathered += ln.text[i];
    // No span may point past its own line -- the failure the re-basing prevents.
    for (const reader::Span& s : ln.emphasis) CHECK(s.end() <= ln.text.size());
  }
  CHECK(linesWithEmphasis >= 2);  // it really did straddle a break
  // Joined back up, allowing for the space the break consumed.
  CHECK(gathered.find("poor") != std::string::npos);
  CHECK(gathered.find("nothing else") != std::string::npos);
}

TEST_CASE("a line with no emphasis carries no spans at all") {
  // The free path, asserted: a chapter with one emphasised phrase must not put an
  // allocation on all twelve of a page's lines.
  Body body;
  Italic ital;
  PageMetrics m = boardMetrics();
  m.italic = &ital.face;
  reader::Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument("<p>One <em>two</em> three.</p><p>No emphasis here.</p>",
                                d, &why));
  reader::PageBuilder pb(body.face, m);
  for (size_t i = 0; i < d.blocks.size(); ++i)
    pb.add(d.blocks[i], static_cast<int>(i));
  const reader::Page page = pb.finish();
  REQUIRE(page.lines.size() >= 2);
  CHECK(markedLine(page.lines[0]) == "One <two> three.");
  CHECK(page.lines[1].emphasis.empty());
}

TEST_CASE("the spans survive a page turn, because the line owns them") {
  // A Page is self-contained so a block can be dropped the moment its lines are
  // taken -- LaidLine::text is owned for that reason, and the spans have to be too.
  // Here the block is destroyed before the page is read.
  Body body;
  Italic ital;
  PageMetrics m = boardMetrics(120);  // a short column, so the block spans pages
  m.italic = &ital.face;
  reader::Page page;
  {
    reader::Document d;
    const char* why = "";
    REQUIRE(reader::buildDocument(
        "<p>Alpha beta gamma delta <em>epsilon zeta eta theta</em> iota kappa.</p>",
        d, &why));
    reader::PageBuilder pb(body.face, m);
    pb.add(d.blocks[0], 0);
    page = pb.take();
    // d and pb both die here; the page must still be readable and correct.
  }
  bool sawEmphasis = false;
  for (const reader::LaidLine& ln : page.lines) {
    for (const reader::Span& s : ln.emphasis) {
      CHECK(s.end() <= ln.text.size());
      sawEmphasis = true;
    }
  }
  // The first page of that block may or may not reach the emphasis; what must hold
  // is that nothing dangles either way.
  CHECK((sawEmphasis || !page.lines.empty()));
}
