#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "grained_source.h"
#include "reader/chapter.h"
#include "reader/document.h"

namespace {

// A document as one line: kind initial plus text, so a whole chapter's structure
// is one assertion. P=paragraph, H=heading, Q=blockquote, L=list item.
std::string flatten(std::string_view xhtml) {
  reader::Document d;
  const char* why = "";
  if (!reader::buildDocument(xhtml, d, &why)) return std::string("!") + why;
  std::string out;
  for (const reader::Block& b : d.blocks) {
    switch (b.kind) {
      case reader::BlockKind::Paragraph: out += "P"; break;
      case reader::BlockKind::Heading: out += "H"; break;
      case reader::BlockKind::Blockquote: out += "Q"; break;
      case reader::BlockKind::ListItem: out += "L"; break;
    }
    out += "[" + b.text + "]";
  }
  return out;
}

bool builds(std::string_view xhtml) {
  reader::Document d;
  const char* why = "";
  return reader::buildDocument(xhtml, d, &why);
}

}  // namespace

TEST_CASE("paragraphs and headings become blocks in reading order") {
  CHECK(flatten("<html><body><h1>One</h1><p>Text.</p></body></html>") ==
        "H[One]P[Text.]");
}

TEST_CASE("every heading level is one Heading, because the board draws one style") {
  CHECK(flatten("<body><h1>a</h1><h3>b</h3><h6>c</h6></body>") == "H[a]H[b]H[c]");
}

TEST_CASE("blockquotes and list items are their own kinds") {
  CHECK(flatten("<body><blockquote>quoted</blockquote>"
                "<ul><li>one</li><li>two</li></ul></body>") ==
        "Q[quoted]L[one]L[two]");
}

TEST_CASE("EMPHASIS CONTRIBUTES ITS TEXT AND NOTHING ELSE") {
  // The slice's honest limit: there is no italic face to render it with, so the
  // sentence reads correctly and reads unemphasised. Pinned so nobody later
  // assumes the marker is in there.
  CHECK(flatten("<p>a <em>b</em> c</p>") == "P[a b c]");
  CHECK(flatten("<p>a <strong>b</strong><i>c</i><b>d</b></p>") == "P[a bcd]");
}

TEST_CASE("whitespace collapses, as it does in HTML") {
  // XHTML is written with the indentation of a source file, and every one of
  // mkepub.py's paragraphs is wrapped across lines. Without collapsing, a
  // paragraph arrives full of newlines and the wrap breaks at the wrong places.
  CHECK(flatten("<p>a   b\n\n  c</p>") == "P[a b c]");
  CHECK(flatten("<p>\n  Miss Brooke\n  had beauty.\n</p>") == "P[Miss Brooke had beauty.]");
}

TEST_CASE("a leading or trailing space in a block is trimmed") {
  CHECK(flatten("<p>  padded  </p>") == "P[padded]");
}

TEST_CASE("A DIALOGUE DASH IS GLUED TO ITS FIRST WORD") {
  // A paragraph that opens with a dash is direct speech, and the dash belongs to
  // the words after it. The space between them is therefore NOT elastic and NOT a
  // break opportunity -- which on this device means U+00A0, because `stretchFor`,
  // `drawRunF26` and `wrapProseLead` all key on U+0020 and nothing else.
  //
  // MEASURED BEFORE IT WAS WRITTEN, over the 225-book corpus: 17,435 paragraphs
  // already ship the non-breaking space themselves (15,053 with an em dash, 2,382
  // with an en dash), and 8,095 ship a plain space instead. So this is not a rule
  // invented here -- it is the majority form, supplied for the books that omitted
  // it. `Le Fleau` is 6,837 of the plain-space ones, and it is what reported this:
  // the gap after the dash was justified along with every other gap on the line,
  // so it swung between one space and five from line to line and read as the
  // indent moving at random. On a line holding only the dash and one long word it
  // reached 164px -- 28 spaces -- which is what made it a bug rather than a taste.
  CHECK(flatten("<p>– On va bien rigoler.</p>") == "P[– On va bien rigoler.]");
  CHECK(flatten("<p>— Yes, I said.</p>") == "P[— Yes, I said.]");
  CHECK(flatten("<p>- Bonjour.</p>") == "P[- Bonjour.]");
}

TEST_CASE("gluing the dash touches nothing else that looks like one") {
  // ALREADY GLUED IS LEFT ALONE, which is most of the corpus: onlyWhitespace()
  // treats U+00A0 as whitespace and the block builder keeps it inside text, so
  // this arrives correct and must stay correct.
  CHECK(flatten("<p>– Deja glued.</p>") == "P[– Deja glued.]");
  // A DASH MID-BLOCK IS PUNCTUATION, not a speaker mark: an em dash sets off a
  // clause and its spaces are ordinary. Only the block's first character opens
  // direct speech.
  CHECK(flatten("<p>She paused — then spoke.</p>") == "P[She paused — then spoke.]");
  // NO SPACE MEANS NO GAP TO GLUE. `-5` is a minus sign and `--` is a rule.
  CHECK(flatten("<p>-5 degrees.</p>") == "P[-5 degrees.]");
  CHECK(flatten("<p>–– twice.</p>") == "P[–– twice.]");
  // A HYPHENATED FIRST WORD IS NOT A DASH EITHER.
  CHECK(flatten("<p>Jean-Marc spoke.</p>") == "P[Jean-Marc spoke.]");
  // AND A BLOCK THAT IS NOTHING BUT A DASH survives the trim without the glue
  // reading past the end of it.
  CHECK(flatten("<body><p>– </p><p>real</p></body>") == "P[–]P[real]");
}

TEST_CASE("an empty block is dropped, not emitted blank") {
  // `<p></p>` and `<p>   </p>` are layout artifacts of a generator, not content,
  // and a blank block would take a line of the page.
  CHECK(flatten("<body><p></p><p>real</p><p>  </p></body>") == "P[real]");
}

TEST_CASE("text outside any block is still content") {
  // A bare text node in the body is not markup a book should lose.
  CHECK(flatten("<body>loose text</body>") == "P[loose text]");
}

TEST_CASE("UNMODELLED ELEMENTS CONTRIBUTE THEIR TEXT, never a guessed layout") {
  // A table becomes its cells in reading order. It is not right, and it is not a
  // guess either -- the alternative is silently losing the content.
  CHECK(flatten("<table><tr><td>a</td><td>b</td></tr></table>") == "P[a b]");
  // A script or a style is NOT content, and its text must never reach the page.
  CHECK(flatten("<body><style>p{color:red}</style><p>x</p></body>") == "P[x]");
  CHECK(flatten("<body><script>var x = 1;</script><p>x</p></body>") == "P[x]");
}

TEST_CASE("the head is not content") {
  CHECK(flatten("<html><head><title>Chapter One</title></head>"
                "<body><p>x</p></body></html>") == "P[x]");
}

TEST_CASE("a nested block closes the one it is inside") {
  // `<blockquote><p>x</p></blockquote>` is one quoted paragraph, not a quote and
  // a paragraph both claiming the same text.
  CHECK(flatten("<blockquote><p>x</p></blockquote>") == "Q[x]");
}

// --- Refusals ---------------------------------------------------------------

TEST_CASE("malformed markup is a refusal with a reason") {
  // THIS TEST'S CASE USED TO BE `&nbsp;`, and an entity the tokenizer does not know
  // is no longer malformed -- it is text. Erroring on one truncated the chapter at
  // that byte, silently, which cost `Dark Plagueis` 177 of its 183 chapters. An
  // unterminated comment is still genuinely malformed, and is what this asserts now.
  reader::Document d;
  const char* why = "";
  CHECK_FALSE(reader::buildDocument("<p>a<!-- unterminated</p>", d, &why));
  CHECK(std::strlen(why) > 0);
}

TEST_CASE("a named entity does not truncate the blocks after it") {
  // THE TEST THAT WOULD HAVE CAUGHT `Dark Plagueis`, which lost 177 of its 183
  // chapters: BlockReader stops on Node::Error, ChapterReader::next() then returns
  // false, and a caller cannot tell that from the chapter ending. Every block after
  // the first entity was discarded, and the book opened looking empty rather than
  // broken.
  //
  // The tokenizer tests prove the bytes; this proves the thing that was lost.
  reader::ChapterReader cr;
  REQUIRE(cr.beginBuffer(
      "<html><body><p>One</p><p>Two&nbsp;three</p><p>Four</p></body></html>"));
  std::vector<std::string> texts;
  reader::Block b;
  while (cr.next(b)) texts.push_back(b.text);
  REQUIRE(texts.size() == 3);
  CHECK(texts[0] == "One");
  CHECK(texts[1] == "Two\xC2\xA0" "three");
  CHECK(texts[2] == "Four");
  CHECK(cr.ok());
}

TEST_CASE("AN UNCLOSED TAG IS CAUGHT HERE, which xml.h leaves to this layer") {
  CHECK_FALSE(builds("<body><p>unclosed</body>"));
  CHECK_FALSE(builds("<p>a"));
  CHECK_FALSE(builds("</p>"));            // a close with no open
  CHECK_FALSE(builds("<a><b></a></b>"));  // mis-nested
}

TEST_CASE("nesting deeper than the cap is a refusal, not a stack overflow") {
  std::string doc;
  for (size_t i = 0; i <= reader::kMaxNestDepth; ++i) doc += "<div>";
  doc += "x";
  for (size_t i = 0; i <= reader::kMaxNestDepth; ++i) doc += "</div>";
  CHECK_FALSE(builds(doc));
}

TEST_CASE("more blocks than the cap is a refusal") {
  std::string doc = "<body>";
  for (size_t i = 0; i <= reader::kMaxBlocks; ++i) doc += "<p>x</p>";
  doc += "</body>";
  CHECK_FALSE(builds(doc));
}

TEST_CASE("a real chapter builds, with the shape mkepub.py wrote") {
  const std::string_view real =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\">"
      "<head><title>One</title><link rel=\"stylesheet\" href=\"style.css\"/></head>"
      "<body><h1>One: The Rope Ferry</h1>"
      "  <p class=\"first\">The ferry was a rope and a flat boat.</p>\n"
      "  <p>Upstream the light came off the surface &#8212; caf&#233;.</p>\n"
      "  <blockquote>A blockquote, indented and italic.</blockquote>\n"
      "  <ul>\n    <li>A list item.</li>\n    <li>A second.</li>\n  </ul>\n"
      "</body></html>";
  reader::Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument(real, d, &why));
  REQUIRE(d.blocks.size() == 6);
  CHECK(d.blocks[0].kind == reader::BlockKind::Heading);
  CHECK(d.blocks[0].text == "One: The Rope Ferry");
  CHECK(d.blocks[1].text == "The ferry was a rope and a flat boat.");
  CHECK(d.blocks[2].text == "Upstream the light came off the surface \xE2\x80\x94 caf\xC3\xA9.");
  CHECK(d.blocks[3].kind == reader::BlockKind::Blockquote);
  CHECK(d.blocks[4].kind == reader::BlockKind::ListItem);
  CHECK(d.blocks[5].kind == reader::BlockKind::ListItem);
}

// --- BlockReader, the resumable form ----------------------------------------
//
// Everything above drives buildDocument, which drains this into a vector -- so it
// covers the PARSING and says nothing about the streaming. What matters here is
// that blocks arrive one at a time and that nothing accumulates behind them.

namespace {

using grainsrc::Grained;

// A chapter with the shape a real one has: a heading, many paragraphs, a quote and
// a list. Long enough that holding all of it would be visible.
std::string chapterOf(int paragraphs) {
  std::string d = "<html><body><h1>Chapter One</h1>";
  for (int i = 0; i < paragraphs; ++i) {
    d += "<p>Paragraph " + std::to_string(i) +
         ", which exists to give the reader something to hand over and forget. "
         "It carries an accent, caf&#233;, and an em dash &#8212; so the decoder "
         "is exercised too.</p>";
  }
  d += "<blockquote>A quotation.</blockquote><ul><li>One</li><li>Two</li></ul>"
       "</body></html>";
  return d;
}

}  // namespace

TEST_CASE("BLOCKS ARRIVE ONE AT A TIME, in the same order buildDocument gives") {
  const std::string doc = chapterOf(40);

  reader::Document whole;
  const char* why = "";
  REQUIRE_MESSAGE(reader::buildDocument(doc, whole, &why), std::string(why));

  Grained src(doc, 64);
  reader::BlockReader r(src);
  std::vector<reader::Block> streamed;
  reader::Block b;
  while (r.next(b)) streamed.push_back(std::move(b));
  REQUIRE(r.ok());

  REQUIRE(streamed.size() == whole.blocks.size());
  CHECK(streamed.size() == 44);  // heading + 40 paragraphs + quote + 2 items
  for (size_t i = 0; i < streamed.size(); ++i) {
    CAPTURE(i);
    CHECK(streamed[i].kind == whole.blocks[i].kind);
    CHECK(streamed[i].text == whole.blocks[i].text);
  }
  CHECK(r.emitted() == static_cast<int>(streamed.size()));
}

TEST_CASE("EVERY GRAIN SIZE GIVES THE SAME BLOCKS") {
  const std::string doc = chapterOf(12);
  std::string reference;
  for (const size_t grain : {size_t{1}, size_t{3}, size_t{97}, size_t{4096}}) {
    Grained src(doc, grain);
    reader::BlockReader r(src);
    std::string flat;
    reader::Block b;
    while (r.next(b)) flat += "[" + b.text + "]";
    REQUIRE(r.ok());
    CAPTURE(grain);
    if (reference.empty()) reference = flat;
    else CHECK(flat == reference);
  }
  CHECK_FALSE(reference.empty());
}

TEST_CASE("THE READER HOLDS ONE BLOCK, NOT THE CHAPTER") {
  // The whole point of the resumable form. Asserted by taking blocks and dropping
  // them: if the reader accumulated, its own footprint would grow with the
  // chapter, and a 400-paragraph chapter would show it.
  //
  // Measured against the DOCUMENT the same chapter would build, which is what this
  // replaces -- the ratio is the saving.
  const std::string doc = chapterOf(400);
  reader::Document whole;
  const char* why = "";
  REQUIRE(reader::buildDocument(doc, whole, &why));
  size_t documentBytes = 0;
  for (const reader::Block& b : whole.blocks) documentBytes += b.text.size();

  Grained src(doc, 512);
  reader::BlockReader r(src);
  reader::Block b;
  size_t largest = 0, total = 0;
  int n = 0;
  while (r.next(b)) {
    if (b.text.size() > largest) largest = b.text.size();
    total += b.text.size();
    ++n;
    b = reader::Block{};  // dropped, as the reader's caller will drop it
  }
  REQUIRE(r.ok());
  CHECK(n == 404);
  CHECK(total == documentBytes);
  CAPTURE(largest);
  CAPTURE(documentBytes);
  // One block against the whole chapter's blocks: the resident cost is the former.
  CHECK(largest < documentBytes / 100);
}

TEST_CASE("the caps still fire in the streaming form") {
  // Same refusals as buildDocument's, reached through the incremental path -- where
  // the block cap in particular is now a running count rather than a vector's size.
  std::string deep;
  for (size_t i = 0; i <= reader::kMaxNestDepth; ++i) deep += "<div>";
  deep += "x";
  Grained d1(deep, 8);
  reader::BlockReader r1(d1);
  reader::Block b;
  while (r1.next(b)) {}
  CHECK_FALSE(r1.ok());

  std::string many = "<body>";
  for (size_t i = 0; i <= reader::kMaxBlocks; ++i) many += "<p>x</p>";
  many += "</body>";
  Grained d2(many, 4096);
  reader::BlockReader r2(d2);
  int count = 0;
  while (r2.next(b)) ++count;
  CHECK_FALSE(r2.ok());
  CHECK(count == static_cast<int>(reader::kMaxBlocks));  // it stopped AT the cap
}

TEST_CASE("A PARAGRAPH OF NOTHING BUT NBSP IS DROPPED") {
  // `<p>&nbsp;</p>` is how an ebook makes vertical space and it is everywhere: the
  // first text chapter of a real EPUB opens with THREE of them, and the device
  // showed a page whose first line was a blank space because each survived as a
  // two-byte block and took a line.
  //
  // U+00A0 is whitespace for THIS question only -- inside text it is kept, because
  // there it is deliberate.
  CHECK(flatten("<body><p>\xC2\xA0</p><p>real</p></body>") == "P[real]");
  CHECK(flatten("<body><p>\xC2\xA0</p><p>\xC2\xA0</p><p>\xC2\xA0</p><p>x</p></body>") == "P[x]");
  CHECK(flatten("<body><p> \xC2\xA0 \n</p><p>x</p></body>") == "P[x]");
  // Kept where it is part of the sentence: French sets one before a colon, and
  // collapsing it would let the line break in the wrong place.
  // SPLIT LITERALS, because `\x` eats unbounded hex digits: "\xA0b" is 0xA0B and
  // does not fit a char, so the compiler rejects it. The lines above happen to be
  // followed by ':' or '<', which are not hex.
  CHECK(flatten("<p>Note\xC2\xA0" ": ceci</p>") == "P[Note\xC2\xA0" ": ceci]");
  CHECK(flatten("<p>a\xC2\xA0" "b</p>") == "P[a\xC2\xA0" "b]");
}

// --- Inline emphasis ---------------------------------------------------------

namespace {

// A block's text with its emphasis spans marked, so a span and the bytes it covers
// are asserted TOGETHER. An offset asserted on its own passes just as happily when
// it points at the wrong letter.
std::string marked(std::string_view xhtml) {
  reader::Document d;
  const char* why = "";
  if (!reader::buildDocument(xhtml, d, &why)) return std::string("!") + why;
  std::string out;
  for (const reader::Block& b : d.blocks) {
    out += "[";
    for (size_t i = 0; i < b.text.size(); ++i) {
      const bool here = reader::emphasisedAt(b.emphasis, i);
      const bool prev = i > 0 && reader::emphasisedAt(b.emphasis, i - 1);
      if (here && !prev) out += "<";
      if (!here && prev) out += ">";
      out += b.text[i];
    }
    if (!b.text.empty() && reader::emphasisedAt(b.emphasis, b.text.size() - 1)) out += ">";
    out += "]";
  }
  return out;
}

}  // namespace

TEST_CASE("em, i and cite are emphasis; strong and b are not") {
  CHECK(marked("<p>a <em>b</em> c</p>") == "[a <b> c]");
  CHECK(marked("<p>a <i>b</i> c</p>") == "[a <b> c]");
  CHECK(marked("<p>a <cite>b</cite> c</p>") == "[a <b> c]");
  // 220 characters across eight real books, six of them with none. Their text still
  // arrives; it is simply not marked.
  CHECK(marked("<p>a <strong>b</strong> c</p>") == "[a b c]");
  CHECK(marked("<p>a <b>b</b> c</p>") == "[a b c]");
}

TEST_CASE("GLUING THE DASH CARRIES THE EMPHASIS SPANS WITH IT") {
  // The glue grows the block by one byte, so every span after it moves. This is
  // `Le Fleau`'s own shape -- `<p>- <i>Brrrrrrrrrroum...</i> Prends ca</p>` -- and
  // an unshifted span would italicise from one byte early, which is a space and
  // therefore INVISIBLE. marked() asserts the span against the bytes it covers
  // for exactly that reason.
  CHECK(marked("<p>– <em>Brrr</em> Prends ca</p>") == "[– <Brrr> Prends ca]");
  // A span that COVERS the glued space grows rather than moves.
  CHECK(marked("<p><em>– Brrr</em> ca</p>") == "[<– Brrr> ca]");
}

TEST_CASE("nested emphasis is ONE run, not two") {
  // `<em><cite>x</cite></em>` is one emphasised phrase, and a flag rather than a
  // depth would close the run on the inner tag and leave the rest roman.
  CHECK(marked("<p>a <em><cite>b c</cite></em> d</p>") == "[a <b c> d]");
  CHECK(marked("<p><em>a <i>b</i> c</em></p>") == "[<a b c>]");
}

TEST_CASE("adjacent emphasised tags stay one word and TWO runs") {
  // document.cpp's own rule: `<em>b</em><i>c</i>` must read as "bc" because that is
  // one word a generator split for styling. Both halves are emphasised, and they are
  // contiguous, so the marker shows one region -- but they are two spans, because
  // nothing merges them and nothing needs to.
  const std::string m = marked("<p><em>b</em><i>c</i></p>");
  CHECK(m == "[<bc>]");
  reader::Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument("<p><em>b</em><i>c</i></p>", d, &why));
  REQUIRE(d.blocks.size() == 1);
  CHECK(d.blocks[0].emphasis.size() == 2);
}

TEST_CASE("an empty emphasis leaves no span") {
  // A zero-length range is something every walk over the vector then has to skip.
  reader::Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument("<p>a<em></em>b</p>", d, &why));
  REQUIRE(d.blocks.size() == 1);
  CHECK(d.blocks[0].text == "ab");
  CHECK(d.blocks[0].emphasis.empty());
}

TEST_CASE("emphasis spanning a block boundary closes at it and reopens") {
  // Real markup, and the alternative is a span indexing a string it does not belong
  // to -- offsets travel with the text they were measured against.
  CHECK(marked("<em><p>a b</p><p>c d</p></em>") == "[<a b>][<c d>]");
}

TEST_CASE("the trailing-space trim clips a span rather than leaving it past the end") {
  // `<p>a <em>b </em></p>` collapses to "a b": the span was opened over "b " and the
  // trim takes the space, so an unclipped span would run one byte past the string.
  // Every test whose text has no trailing space passes either way.
  reader::Document d;
  const char* why = "";
  REQUIRE(reader::buildDocument("<p>a <em>b </em></p>", d, &why));
  REQUIRE(d.blocks.size() == 1);
  CHECK(d.blocks[0].text == "a b");
  REQUIRE(d.blocks[0].emphasis.size() == 1);
  const reader::Span& s = d.blocks[0].emphasis[0];
  CHECK(s.off == 2);
  CHECK(s.len == 1);
  CHECK(s.end() <= d.blocks[0].text.size());
}

TEST_CASE("emphasis inside a suppressed element never reaches a block") {
  // `<style>` text must not reach a page, and neither must a span pointing into it.
  CHECK(marked("<head><style><em>x</em></style></head><p>a</p>") == "[a]");
}

TEST_CASE("emphasis in a heading, a blockquote and a list item") {
  // The spans are independent of the kind, and asserting it here is what stops a
  // later change from wiring them to Paragraph only.
  CHECK(marked("<h1>a <em>b</em></h1>") == "[a <b>]");
  CHECK(marked("<blockquote><p>a <em>b</em></p></blockquote>") == "[a <b>]");
  CHECK(marked("<li>a <em>b</em></li>") == "[a <b>]");
}

TEST_CASE("too much emphasis in one block is refused, not truncated") {
  std::string x = "<p>";
  for (size_t i = 0; i <= reader::kMaxEmphasisPerBlock; ++i) x += "<em>a</em>-";
  x += "</p>";
  reader::Document d;
  const char* why = "";
  CHECK_FALSE(reader::buildDocument(x, d, &why));
  CHECK(std::strstr(why, "emphasis") != nullptr);
}

// --- A BLOCK OVER kMaxBlockBytes (issue #37) ---------------------------------
//
// The cap used to set `error_`, which stops BlockReader, which ends the chapter --
// and `next()` returning false is also how a chapter ends normally, so NOTHING
// reported it. It is the named-entity bug's exact shape, and it cost two Gutenberg
// mathematics texts in the 225-book corpus everything after one paragraph of a
// hundred thousand digits.
//
// A run of digits is what those books really contain, and it is also the case with
// no break opportunity anywhere in it -- so a split cannot be made to land on a
// space, which is why it lands on the cap.
namespace {

std::string digits(size_t n) {
  std::string s;
  s.reserve(n);
  for (size_t i = 0; i < n; ++i) s += static_cast<char>('0' + (i % 10));
  return s;
}

// A chapter's blocks in order, their kinds, and how many splits it took -- what a
// split must leave untouched apart from where one block becomes two.
struct Walked {
  std::vector<std::string> texts;
  std::vector<reader::BlockKind> kinds;
  bool ok = false;
  size_t split = 0;
};

Walked walkBlocks(const std::string& doc, size_t grain = 4096) {
  Walked w;
  Grained src(doc, grain);
  reader::BlockReader r(src);
  reader::Block b;
  while (r.next(b)) {
    w.texts.push_back(b.text);
    w.kinds.push_back(b.kind);
  }
  w.ok = r.ok();
  w.split = r.blocksSplit();
  return w;
}

}  // namespace

TEST_CASE("A BLOCK OVER THE CAP DOES NOT END THE CHAPTER") {
  // THE TEST THAT WOULD HAVE CAUGHT THE TWO CORPUS BOOKS. Everything after the
  // over-long paragraph was discarded and the chapter read as though it had ended
  // there, which no caller can tell from a chapter that really has.
  const std::string doc = "<html><body><p>One</p><p>" +
                          digits(reader::kMaxBlockBytes + 5000) +
                          "</p><p>Last</p></body></html>";
  reader::ChapterReader cr;
  REQUIRE(cr.beginBuffer(doc));
  std::vector<std::string> texts;
  reader::Block b;
  while (cr.next(b)) texts.push_back(b.text);
  CHECK(cr.ok());
  REQUIRE(texts.size() >= 3);
  CHECK(texts.front() == "One");
  CHECK(texts.back() == "Last");
}

TEST_CASE("THE OVER-LONG BLOCK IS SPLIT, so no byte of the book is lost") {
  // Split rather than truncated, because what the cap protects is the size of ONE
  // block and both halves are under it -- so the heap peak is unchanged and the text
  // is all still there. Truncating would have kept the reporting and thrown the
  // bytes away, and its magnitude is unbounded: a chapter that is one giant <div>
  // with no <p> is ONE block, and a real book's longest chapter is 228,849 bytes of
  // blocks.
  const std::string run = digits(reader::kMaxBlockBytes + 5000);
  const Walked w =
      walkBlocks("<html><body><p>One</p><p>" + run + "</p><p>Last</p></body></html>");
  CHECK(w.ok);
  REQUIRE(w.texts.size() >= 4);  // One, two or more pieces of the run, Last

  std::string joined;
  for (size_t i = 1; i + 1 < w.texts.size(); ++i) joined += w.texts[i];
  CHECK(joined == run);

  // EVERY PIECE IS STILL UNDER THE CAP, which is the whole reason the cap exists and
  // the whole reason a split preserves what a refusal was protecting. This fixture has
  // no spaces and no dialogue dash, so the two bytes document.h allows an emitted block
  // above the cap are not in play and the bound is exact here.
  for (const std::string& t : w.texts) CHECK(t.size() <= reader::kMaxBlockBytes);

  // ...and none of them turned into some other kind of block on the way.
  for (const reader::BlockKind k : w.kinds) CHECK(k == reader::BlockKind::Paragraph);
}

TEST_CASE("A SPLIT IS COUNTED, so it is not silent") {
  // Xml::attrsDropped()'s shape and its reason: a caller that finds more blocks than
  // the book has paragraphs can tell "the book wrote them" from "we could not hold
  // what it wrote". Zero for every corpus book but the two.
  const Walked over =
      walkBlocks("<body><p>" + digits(reader::kMaxBlockBytes + 5000) + "</p></body>");
  CHECK(over.split == 1);
  CHECK(over.texts.size() == 2);

  const Walked twice = walkBlocks(
      "<body><p>" + digits(2 * reader::kMaxBlockBytes + 5000) + "</p></body>");
  CHECK(twice.split == 2);
  CHECK(twice.texts.size() == 3);

  // A RUN THAT IS AN EXACT MULTIPLE OF THE CAP makes no extra cut, and this is the
  // assertion that pins WHERE the cut is made: it is made when a byte arrives with the
  // block already full, never when the block merely becomes full, so the continuation
  // always receives that byte and an exhausted element leaves nothing behind.
  const Walked exact =
      walkBlocks("<body><p>" + digits(2 * reader::kMaxBlockBytes) + "</p></body>");
  CHECK(exact.split == 1);
  CHECK(exact.texts.size() == 2);
  CHECK(exact.texts[0].size() == reader::kMaxBlockBytes);
  CHECK(exact.texts[1].size() == reader::kMaxBlockBytes);

  const Walked under = walkBlocks("<body><p>One</p><p>Two</p></body>");
  CHECK(under.split == 0);
  CHECK(under.texts.size() == 2);
}

TEST_CASE("a split block keeps the kind the stack gave it") {
  // The seam re-derives the kind from the tag stack rather than remembering it, so a
  // split blockquote is two blockquotes and not a quote followed by prose -- which is
  // how a quote's styling silently disappears from a book.
  for (const char* tag : {"blockquote", "li", "h1"}) {
    const std::string t = tag;
    const Walked w = walkBlocks("<body><" + t + ">" +
                                digits(reader::kMaxBlockBytes + 3000) + "</" + t +
                                "></body>");
    CHECK(w.ok);
    REQUIRE(w.kinds.size() == 2);
    CHECK(w.kinds[0] == w.kinds[1]);
    CHECK(w.kinds[0] != reader::BlockKind::Paragraph);
  }
}

TEST_CASE("emphasis open across a split closes at the seam and reopens") {
  // The rule `<em><p>a</p><p>b</p></em>` already states, reached from the other
  // direction: a span's offsets index the string they were measured against, so one
  // carried across the seam would index a block it does not belong to.
  const std::string run = digits(reader::kMaxBlockBytes + 3000);
  const std::string doc = "<body><p><em>" + run + "</em></p></body>";
  Grained src(doc, 4096);
  reader::BlockReader r(src);
  reader::Block b;
  std::vector<size_t> lens;
  while (r.next(b)) {
    REQUIRE(b.emphasis.size() == 1);
    CHECK(b.emphasis[0].off == 0);
    CHECK(b.emphasis[0].len == b.text.size());
    lens.push_back(b.text.size());
  }
  CHECK(r.ok());
  REQUIRE(lens.size() == 2);
  CHECK(lens[0] + lens[1] == run.size());
}
