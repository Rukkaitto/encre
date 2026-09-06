#include <string>
#include <vector>

#include "doctest.h"
#include "grained_source.h"
#include "reader/xml.h"

using reader::Xml;
using Node = reader::Xml::Node;

namespace {

// The whole document as a flat list, which is how a stack machine above this will
// actually consume it. `<p>` becomes "(p", text becomes "'text", `</p>` becomes
// ")p" -- compact enough to assert a whole document on one line.
std::string flatten(std::string_view doc) {
  Xml x(doc);
  std::string out;
  for (;;) {
    switch (x.next()) {
      case Node::StartTag: out += "(" + std::string(x.name()); break;
      case Node::EndTag: out += ")" + std::string(x.name()); break;
      case Node::Text: out += "'" + std::string(x.text()); break;
      case Node::Eof: return out;
      case Node::Error: return out + "!" + x.error();
    }
  }
}

bool parses(std::string_view doc) {
  Xml x(doc);
  for (;;) {
    const Node n = x.next();
    if (n == Node::Eof) return true;
    if (n == Node::Error) return false;
  }
}

}  // namespace

TEST_CASE("elements and text come out in document order") {
  CHECK(flatten("<p>hello</p>") == "(p'hello)p");
  CHECK(flatten("<a><b>x</b></a>") == "(a(b'x)b)a");
}

TEST_CASE("a self-closing tag yields a start AND an end") {
  // So a caller's stack balances without knowing the tag was self-closing --
  // which is the whole point, since <br/> and <br></br> mean the same thing.
  CHECK(flatten("<br/>") == "(br)br");
  CHECK(flatten("<p>a<br/>b</p>") == "(p'a(br)br'b)p");
  CHECK(flatten("<img src='x.png' />") == "(img)img");
}

TEST_CASE("attributes are read by name, and absent differs from empty") {
  Xml x("<item id='ch1' href='' />");
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.name() == "item");
  CHECK(x.attr("id") == "ch1");
  CHECK(x.hasAttr("href"));
  CHECK(x.attr("href") == "");
  CHECK_FALSE(x.hasAttr("media-type"));
  CHECK(x.attr("media-type") == "");
  CHECK(x.attrCount() == 2);
}

TEST_CASE("both quote styles, and either inside the other") {
  Xml x("<a p=\"it's\" q='say \"hi\"'/>");
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.attr("p") == "it's");
  CHECK(x.attr("q") == "say \"hi\"");
}

TEST_CASE("the five predefined entities decode, in text and in attributes") {
  Xml x("<p title='a&amp;b'>&lt;tag&gt; &quot;q&quot; &apos;a&apos;</p>");
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.attr("title") == "a&b");
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "<tag> \"q\" 'a'");
}

TEST_CASE("numeric character references decode, decimal and hex") {
  // &#8212; is an em dash and &#x2014; is the same character -- and the fixtures
  // mkepub.py writes contain one, so this is not hypothetical.
  Xml x("<p>a&#8212;b&#x2014;c&#233;</p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "a\xE2\x80\x94" "b\xE2\x80\x94" "c\xC3\xA9");
}

TEST_CASE("the HTML 4 named entities decode, in text and in attributes") {
  // MEASURED, not chosen: seven distinct named entities appear across sixteen real
  // books -- rsquo, nbsp, mdash, ndash, ldquo, rdquo, lsquo, 50,245 occurrences --
  // and every one is HTML 4 punctuation. The table is all 252 because the whole set
  // costs 1,416 bytes of names and typing a subset invites a second pass.
  Xml x("<p title='a&nbsp;b'>x&rsquo;y &mdash; &ndash; &ldquo;q&rdquo;</p>");
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.attr("title") == "a\xC2\xA0" "b");
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "x\xE2\x80\x99" "y \xE2\x80\x94 \xE2\x80\x93 \xE2\x80\x9C" "q\xE2\x80\x9D");
}

TEST_CASE("an entity we still do not know passes through as its own text") {
  // THE RULE THIS REVERSES is in decodeEntity's own header: an unknown entity meant
  // "we are wrong about the file, not that the file is being casual". Half of that
  // is right and the table above is the half that acts on it. The other half was
  // measured and is false: `Dark Plagueis` lost 177 of its 183 chapters this way,
  // because erroring here truncates the chapter at that byte.
  //
  // A visible wrong beats an invisible one, which is the call css.h already makes
  // for over-matched italics: a literal "&unknown;" on the page is a typographic
  // error you can see and report, where a silently discarded chapter is not.
  Xml x("<p>Tom &unknown; Jerry</p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "Tom &unknown; Jerry");
}

TEST_CASE("a bare ampersand survives, because real books contain them") {
  // Not an entity at all: no ';' before the run ends. Today this errors and takes
  // the chapter with it.
  Xml x("<p>Tom & Jerry</p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "Tom & Jerry");
}

TEST_CASE("a malformed numeric reference passes through rather than truncating") {
  // `&#;` and `&#zz;` terminate, so they reach the numeric branch and fail to parse
  // there -- which was the same chapter-ending error by another route. Every exit
  // from decodeEntity that is not a decoded character is now text.
  CHECK(parses("<p>a&#;b</p>"));
  CHECK(parses("<p>a&#zz;b</p>"));
  Xml x("<p>a&#;b</p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "a&#;b");
}

TEST_CASE("a namespace prefix is stripped, because EPUB's decorate a known vocabulary") {
  CHECK(flatten("<opf:package><dc:title>x</dc:title></opf:package>") ==
        "(package(title'x)title)package");
}

TEST_CASE("the declaration, DOCTYPE, comments and PIs are skipped") {
  CHECK(flatten("<?xml version='1.0' encoding='utf-8'?><p>x</p>") == "(p'x)p");
  CHECK(flatten("<!DOCTYPE html><p>x</p>") == "(p'x)p");
  CHECK(flatten("<p>a<!-- a comment -->b</p>") == "(p'a'b)p");
  CHECK(flatten("<?php nonsense ?><p>x</p>") == "(p'x)p");
}

TEST_CASE("a BOM is skipped") {
  CHECK(flatten("\xEF\xBB\xBF<p>x</p>") == "(p'x)p");
}

TEST_CASE("CDATA is text, undecoded") {
  // Its whole purpose is to hold characters that would otherwise be markup.
  Xml x("<p><![CDATA[a < b & c]]></p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "a < b & c");
}

TEST_CASE("whitespace between elements is text, because in a paragraph it matters") {
  // `<em>a</em> <em>b</em>` has a space that is part of the sentence. A parser
  // that dropped inter-element whitespace would join words.
  CHECK(flatten("<p><em>a</em> <em>b</em></p>") == "(p(em'a)em' (em'b)em)p");
}

// --- Refusals ---------------------------------------------------------------

TEST_CASE("malformed input is a clean Error with an offset, never an abort") {
  const char* cases[] = {
      "<p", "<>", "</>", "<p></", "<p attr>", "<p attr=>",
      "<p attr='unterminated>", "<!-- unterminated", "<![CDATA[oops",
      "<?pi unterminated",
      // `<p>&#xZZ;</p>` WAS HERE and is no longer an error: a numeric reference that
      // does not parse is passed through as its own text, because erroring ended the
      // chapter. It is asserted as text above instead.
  };
  for (const char* c : cases) {
    Xml x(c);
    Node n = x.next();
    while (n != Node::Eof && n != Node::Error) n = x.next();
    // CAPTURE as a std::string, not a `const char*`: doctest resolves the pointer
    // through its `const void*` overload and logs an address, which is what this
    // did while I was hunting for which case failed. test_json.cpp had already
    // worked around the same thing locally.
    CAPTURE(std::string(c));
    CHECK(n == Node::Error);
    CHECK(std::string(x.error()).size() > 0);
  }
}

TEST_CASE("UNBALANCED TAGS ARE NOT THIS LAYER'S ERROR, and that is deliberate") {
  // This is a tokenizer. It emits StartTag, Text, Eof for `<p>unclosed` and keeps
  // no tag stack, so it cannot know a close is missing -- and the document builder
  // above it keeps a stack anyway to know where to attach a node, so it notices at
  // Eof for free. Tracking depth here would be a second copy of that stack.
  //
  // Pinned rather than left implicit, because a caller that assumed otherwise
  // would be relying on a check nobody makes.
  CHECK(flatten("<p>unclosed") == "(p'unclosed");
  CHECK(parses("<p>unclosed"));
  CHECK(parses("<a><b></a></b>"));   // mis-nested, and still just tokens
  CHECK(parses("</p>"));             // a close with no open
}

TEST_CASE("an empty document is Eof, not an error") {
  Xml x("");
  CHECK(x.next() == Node::Eof);
}

TEST_CASE("more attributes than the cap is a drop, by the same rule as the bytes") {
  // ONE RULE, NOT TWO: an attribute this parser cannot hold reads as absent,
  // whether what ran out was the byte buffer or the slot table. The two caps sit
  // four lines apart in the header and a reader who learned one would assume the
  // other. Observed maximum on one tag across 226 real EPUBs is EIGHT, on an
  // <html> carrying namespace declarations -- so this cap has never been reached
  // by a real book, and the shape it would meet first is more xmlns nobody reads.
  std::string doc = "<a";
  for (size_t i = 0; i <= Xml::kMaxAttrs; ++i) doc += " a" + std::to_string(i) + "='v'";
  doc += "/>";
  CHECK(parses(doc));

  Xml x(doc);
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.attr("a0") == "v");
  CHECK(x.attr("a" + std::to_string(Xml::kMaxAttrs - 1)) == "v");
  CHECK_FALSE(x.hasAttr("a" + std::to_string(Xml::kMaxAttrs)));
  CHECK(x.attrsDropped() == 1);
}

TEST_CASE("a name longer than the cap is a refusal") {
  const std::string doc = "<" + std::string(Xml::kMaxNameBytes + 1, 'x') + "/>";
  CHECK_FALSE(parses(doc));
}

TEST_CASE("deterministic fuzz: every truncation and byte flip is survivable") {
  // The same fuzz that found real defects in the JSON reader. The property is
  // only that it terminates and never reports Eof on a broken document.
  const std::string valid =
      "<?xml version='1.0'?><html><body><h1>One</h1>"
      "<p class='first'>Miss &quot;Brooke&quot; &#8212; caf&#233;<em>x</em></p>"
      "<![CDATA[raw < text]]></body></html>";
  for (size_t cut = 0; cut <= valid.size(); ++cut) {
    Xml x(std::string_view(valid).substr(0, cut));
    int guard = 0;
    Node n = x.next();
    while (n != Node::Eof && n != Node::Error && ++guard < 10000) n = x.next();
    CHECK(guard < 10000);  // it must terminate
  }
  for (size_t i = 0; i < valid.size(); ++i) {
    std::string m(valid);
    m[i] = static_cast<char>(m[i] ^ 0x40);
    Xml x(m);
    int guard = 0;
    Node n = x.next();
    while (n != Node::Eof && n != Node::Error && ++guard < 10000) n = x.next();
    CHECK(guard < 10000);
  }
}

// --- The streaming path ------------------------------------------------------
//
// Everything above uses the `std::string_view` constructor, whose BufferSource
// satisfies every read in full. That exercises the tokenizer but NOT the reader
// underneath it: no lookahead ever straddles a refill, so the compaction path, a
// tag name split across two reads, an entity split mid-reference and a BOM
// arriving one byte at a time are all untested by it.
//
// A source that hands out one byte per call makes every one of those happen on
// every token of every document.

namespace {

using grainsrc::Grained;

std::string flattenGrained(std::string_view doc, size_t grain) {
  Grained src(doc, grain);
  Xml x(src);
  std::string out;
  for (;;) {
    switch (x.next()) {
      case Node::StartTag: out += "(" + std::string(x.name()); break;
      case Node::EndTag: out += ")" + std::string(x.name()); break;
      case Node::Text: out += "'" + std::string(x.text()); break;
      case Node::Eof: return out;
      case Node::Error: return out + "!" + x.error();
    }
  }
}

// The documents worth running at every grain: each one puts a different construct
// across a potential read boundary.
const char* const kCorpus[] = {
    "<html><body><p>Miss Brooke had that kind of beauty.</p></body></html>",
    "\xEF\xBB\xBF<p>a BOM, which is three bytes and can be split by any of them</p>",
    "<p>entities: &amp; &lt; &gt; &quot; &apos; &#233; &#x2014; &#8212;</p>",
    "<!-- a comment whose terminator --> <p>follows it</p>",
    "<![CDATA[a CDATA section holding <not-a-tag> and ]] and ]>]]><p>after</p>",
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE html [ <!ENTITY x \"y\"> ]><p>x</p>",
    "<item id=\"ch1\" href=\"OEBPS/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>",
    "<a><b/><c></c><d attr='single quoted'/></a>",
    "<dc:title xmlns:dc=\"http://purl.org/dc/elements/1.1/\">A Prefixed Name</dc:title>",
    "<p>text with a <em>nested</em> run and trailing space </p>",
};

}  // namespace

TEST_CASE("EVERY GRAIN SIZE YIELDS THE SAME TOKENS, one byte at a time included") {
  for (const char* doc : kCorpus) {
    const std::string whole = flatten(doc);
    CAPTURE(std::string(doc));
    for (const size_t grain : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{64},
                               size_t{512}, size_t{4096}}) {
      CAPTURE(grain);
      CHECK(flattenGrained(doc, grain) == whole);
    }
  }
}

TEST_CASE("a BOM split across reads is still skipped") {
  // Three bytes, so grains 1 and 2 both split it. Without the compaction in
  // ensure() the first tag would not match and the BOM would arrive as text.
  const std::string_view doc = "\xEF\xBB\xBF<p>x</p>";
  CHECK(flattenGrained(doc, 1) == "(p'x)p");
  CHECK(flattenGrained(doc, 2) == "(p'x)p");
  CHECK(flattenGrained(doc, 3) == "(p'x)p");
}

TEST_CASE("A LONG TEXT RUN ARRIVES AS SEVERAL NODES, not as a refusal") {
  // kTextBytes is a chunking granularity, not a limit -- the header says so, and
  // this is what makes that true. A caller that accumulates text already handles
  // several nodes in a row, because `a <em>b</em> c` is three of them.
  const size_t n = Xml::kTextBytes * 3 + 17;
  std::string doc = "<p>";
  doc.append(n, 'x');
  doc += "</p>";

  Grained src(doc, 512);
  Xml x(src);
  REQUIRE(x.next() == Node::StartTag);
  size_t got = 0;
  int nodes = 0;
  for (;;) {
    const Node t = x.next();
    if (t == Node::Text) {
      got += x.text().size();
      CHECK(x.text().size() <= Xml::kTextBytes);
      ++nodes;
      continue;
    }
    REQUIRE(t == Node::EndTag);
    break;
  }
  CHECK(got == n);
  CHECK(nodes >= 4);  // really split, not delivered whole
}

TEST_CASE("an entity is never split across two Text nodes") {
  // The buffer stops with room for the longest decoded character, so a reference
  // cannot straddle a node boundary -- otherwise a caller joining two nodes would
  // see a half-decoded character, or the parser would refuse a valid document
  // depending only on where the run happened to land.
  //
  // Built so an entity sits exactly at the boundary, then walked one byte either
  // side of it, so the case is hit rather than hoped for.
  for (int slack = -6; slack <= 6; ++slack) {
    const int fill = static_cast<int>(Xml::kTextBytes) - 4 + slack;
    if (fill < 1) continue;
    std::string doc = "<p>";
    doc.append(static_cast<size_t>(fill), 'x');
    doc += "&#233;y</p>";
    Grained src(doc, 64);
    Xml x(src);
    REQUIRE(x.next() == Node::StartTag);
    std::string joined;
    for (;;) {
      const Node t = x.next();
      if (t == Node::Text) {
        joined += x.text();
        continue;
      }
      CAPTURE(slack);
      REQUIRE(t == Node::EndTag);
      break;
    }
    CAPTURE(slack);
    CHECK(joined == std::string(static_cast<size_t>(fill), 'x') + "\xC3\xA9y");
  }
}

TEST_CASE("an attribute too big for the buffer is DROPPED, not a refusal") {
  // It cannot be split -- attr() answers about the whole tag -- and for two phases
  // the conclusion drawn from that was "therefore refuse the document". There is a
  // third answer: report it ABSENT, which every caller of this parser already
  // handles, and which no caller can be hurt by for an attribute it never reads.
  //
  // Measured over 226 real EPUBs: every tag carrying more than 400 bytes of
  // attributes is a Calibre `<meta name="calibre:user_metadata:#...">`, and the
  // fattest tag that is NOT one is an `<html>` at 379 bytes of namespace
  // declarations. So the only thing this cap ever refuses is metadata, and it
  // refused the whole book with it.
  std::string doc = "<item";
  for (int i = 0; i < 12; ++i) doc += " attribute" + std::to_string(i) + "=\"" +
                                     std::string(60, 'v') + "\"";
  doc += "/>";
  CHECK(parses(doc));
  // And a realistic tag is nowhere near it, so nothing is dropped from one.
  Xml x("<item id=\"ch1\" href=\"OEBPS/ch1.xhtml\" "
        "media-type=\"application/xhtml+xml\" properties=\"nav\"/>");
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.attr("href") == "OEBPS/ch1.xhtml");
  CHECK(x.attrsDropped() == 0);
}

TEST_CASE("a dropped attribute reads as ABSENT, never as a truncated value") {
  // The distinction is the whole reason this is a drop and not a clamp: a
  // truncated href resolves to a path that is wrong rather than to nothing, and a
  // caller cannot tell a short value from a cut one. `hasAttr` exists precisely to
  // tell absent from empty, so it must say absent here.
  // NAMED, because Xml's string_view constructor holds a view and a temporary
  // std::string would be freed before the first next(). This project has already
  // shipped that exact lifetime bug once, in Home's wrapped title.
  const std::string doc =
      "<meta name='calibre:user_metadata' content='" +
      std::string(Xml::kMaxAttrBytes, 'v') + "'/>";
  Xml x(doc);
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.name() == "meta");
  CHECK(x.attr("name") == "calibre:user_metadata");  // the one before it survives
  CHECK_FALSE(x.hasAttr("content"));
  CHECK(x.attr("content") == "");
  CHECK(x.attrsDropped() == 1);
  CHECK(x.next() == Node::EndTag);
  CHECK(x.next() == Node::Eof);
}

TEST_CASE("dropping an attribute gives its bytes back to the ones after it") {
  // The order of a tag's attributes is the author's, so the blob can come FIRST --
  // and if a drop did not rewind the packing offset, everything after it would be
  // dropped too and a book would lose the href it needed for the metadata it did
  // not. This is the case that fails if the rewind is missing.
  const std::string doc = "<item content='" + std::string(Xml::kMaxAttrBytes, 'v') +
                          "' id='ch1' href='OEBPS/ch1.xhtml'/>";
  Xml x(doc);
  REQUIRE(x.next() == Node::StartTag);
  CHECK_FALSE(x.hasAttr("content"));
  CHECK(x.attr("id") == "ch1");
  CHECK(x.attr("href") == "OEBPS/ch1.xhtml");
  CHECK(x.attrsDropped() == 1);
}

TEST_CASE("the real Calibre meta that refused two books in one library") {
  // `Walden` and `Le soleil et l'acier`, both from the same shelf, both refused with
  // "the OPF is malformed" over ONE <meta> of Calibre custom-column JSON: 574 and
  // 489 decoded bytes of `content` against a 512-byte cap. Entity-heavy, because
  // the JSON's every quote is written `&quot;` -- which is what makes the raw
  // attribute nearly twice its decoded length.
  std::string blob;
  while (blob.size() < 700) blob += "&quot;is_multiple&quot;: null, ";
  const std::string doc =
      "<package><metadata>"
      "<meta name='calibre:user_metadata:#formats' content='{" + blob + "}'/>"
      "<meta name='cover' content='book-cover'/>"
      "</metadata></package>";
  Xml x(doc);
  REQUIRE(x.next() == Node::StartTag);  // package
  REQUIRE(x.next() == Node::StartTag);  // metadata
  REQUIRE(x.next() == Node::StartTag);  // the blob
  CHECK(x.attr("name") == "calibre:user_metadata:#formats");
  CHECK_FALSE(x.hasAttr("content"));
  REQUIRE(x.next() == Node::EndTag);
  REQUIRE(x.next() == Node::StartTag);  // and the NEXT meta is read normally
  CHECK(x.attr("name") == "cover");
  CHECK(x.attr("content") == "book-cover");
  CHECK(x.attrsDropped() == 1);
}

TEST_CASE("a name at the cap parses and one past it is refused") {
  const std::string ok(Xml::kMaxNameBytes, 'n');
  const std::string over(Xml::kMaxNameBytes + 1, 'n');
  CHECK(parses("<" + ok + "/>"));
  CHECK_FALSE(parses("<" + over + "/>"));
}

TEST_CASE("offset() counts bytes of the DECOMPRESSED document, not of the buffer") {
  // It is what a page index would key on, so it has to mean the same thing whether
  // the source is a buffer or an inflater -- and in particular it must not report
  // the read-ahead the reader is holding.
  const std::string_view doc = "<p>abc</p>";
  Grained src(doc, 3);
  Xml x(src);
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.offset() == 3);  // just past "<p>"
  REQUIRE(x.next() == Node::Text);
  CHECK(x.offset() == 6);
  REQUIRE(x.next() == Node::EndTag);
  CHECK(x.offset() == doc.size());
}

TEST_CASE("THE PARSER'S SIZE IS FIXED, and small enough to be a local") {
  // Its whole memory is its buffers, whatever the document's length -- that is the
  // point of the rewrite. Pinned because the failure mode of a buffer growing is
  // silent: it costs stack at every call site, and this project has already had one
  // stack-protection panic from arrays living in the wrong place.
  //
  // Measured over a real book, the peak for the WHOLE chain (this, an Inflater's
  // 32 KB window and its tables) is 36,956 bytes for any chapter, against 546,000
  // for the same book when the tokenizer took a buffer.
  CAPTURE(sizeof(Xml));
  CHECK(sizeof(Xml) <= 3072);
  // And the buffers really are the bulk of it, so the number above is not measuring
  // something else that happens to fit.
  CHECK(sizeof(Xml) >= Xml::kTextBytes + Xml::kMaxAttrBytes);
}
