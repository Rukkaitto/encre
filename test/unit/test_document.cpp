#include <cstring>
#include <string>

#include "doctest.h"
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
  reader::Document d;
  const char* why = "";
  CHECK_FALSE(reader::buildDocument("<p>a&nbsp;b</p>", d, &why));
  CHECK(std::strlen(why) > 0);
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

class Grained : public reader::ByteSource {
 public:
  Grained(std::string_view b, size_t grain) : b_(b), grain_(grain) {}
  size_t read(void* dst, size_t bytes) override {
    const size_t want = bytes < grain_ ? bytes : grain_;
    const size_t got = b_.size() - at_ < want ? b_.size() - at_ : want;
    for (size_t i = 0; i < got; ++i) static_cast<char*>(dst)[i] = b_[at_ + i];
    at_ += got;
    return got;
  }

 private:
  std::string_view b_;
  size_t grain_;
  size_t at_ = 0;
};

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
