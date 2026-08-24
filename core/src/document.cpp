#include "reader/document.h"

#include <new>

#include "reader/xml.h"

namespace reader {
namespace {

// THREE CATEGORIES OF ELEMENT, and naming them is the whole design. Every tag an
// EPUB can contain falls into one of these or into "transparent", and transparent
// is the default -- so an element nobody listed contributes its text and no
// structure, which is the DROPPED-not-approximated rule from document.h expressed
// as code.

// Starts a block. The text after it belongs to a new paragraph-like thing.
bool startsBlock(std::string_view t) {
  return t == "p" || t == "div" || t == "blockquote" || t == "li" || t == "pre" ||
         t == "tr" ||  // a table row reads as a paragraph; see below
         t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6";
}

// Does not start a block, but forces a word boundary. `<td>a</td><td>b</td>` has
// no whitespace between the cells and must not read as "ab" -- whereas
// `<em>b</em><i>c</i>` must read as "bc", because that is one word a generator
// split for styling. The difference is exactly this list.
bool separatesWords(std::string_view t) {
  return t == "td" || t == "th" || t == "br";
}

// EMPHASIS. `<cite>` is here because a cited title is set in italics by every
// convention this face was designed for, and books use it that way.
//
// `<strong>` AND `<b>` ARE ABSENT ON PURPOSE, and the measurement is in document.h:
// 17 runs and 220 characters across eight real books. They keep contributing their
// text with no marker, which is what they always did.
bool isEmphasis(std::string_view t) {
  return t == "em" || t == "i" || t == "cite";
}

// NOT CONTENT. Their text must never reach a page: a stylesheet rendered as a
// paragraph is the most obvious way a reader can look broken.
bool isSuppressed(std::string_view t) {
  return t == "head" || t == "style" || t == "script";
}

// ONE TAG NAME, HELD BY VALUE.
//
// The stack used to hold `std::string_view`s of the names, which worked only while
// Xml handed out views into the caller's whole document. It reads a stream now, so
// a name lives in a buffer the next token overwrites -- and the two tests that
// caught it are worth naming, because neither looks like a lifetime bug:
// `<blockquote><p>x</p></blockquote>` came out as a plain paragraph (the stack's
// "blockquote" had become "p"), and `<a><b></a></b>` was ACCEPTED, because the
// mismatch check was comparing two views into the same buffer and they are always
// equal. A dangling view does not crash here; it silently agrees with itself.
//
// TRUNCATED AT 24 BYTES, which is longer than every element name that exists in
// the vocabularies an EPUB can contain: `blockquote` and `figcaption` are 10,
// MathML's `annotation-xml` is 14, and the longest name measured in a real book is
// 14. SVG's `feComponentTransfer` is 19. So two names collide only if they share
// their first 24 bytes AND their length, which no real pair does -- and the cost of
// getting that wrong is one accepted mis-nesting, not a corrupt page.
constexpr size_t kTagNameBytes = 24;

struct TagName {
  char bytes[kTagNameBytes];
  uint8_t len = 0;

  void set(std::string_view s) {
    len = static_cast<uint8_t>(s.size() < kTagNameBytes ? s.size() : kTagNameBytes);
    for (size_t i = 0; i < len; ++i) bytes[i] = s[i];
    // The FULL length is what makes truncation safe: two names sharing a prefix
    // but differing in length still compare unequal.
    full = static_cast<uint16_t>(s.size());
  }
  bool operator==(std::string_view s) const {
    if (s.size() != full) return false;
    const size_t n = s.size() < kTagNameBytes ? s.size() : kTagNameBytes;
    for (size_t i = 0; i < n; ++i)
      if (bytes[i] != s[i]) return false;
    return true;
  }
  std::string_view view() const { return {bytes, len}; }

  uint16_t full = 0;
};

// Whether a block holds nothing but whitespace, WITH U+00A0 COUNTING AS
// WHITESPACE.
//
// `<p>&nbsp;</p>` is how an ebook makes vertical space, and it is everywhere: the
// first text chapter of a real EPUB opens with THREE of them. Trimming only ASCII
// space left each one as a two-byte block, which took a line of the page and drew
// blank -- the device showed a page whose first line was a space.
//
// NBSP is only whitespace for THIS question. It is kept inside text, because there
// it is deliberate: French sets a non-breaking space before a colon, and collapsing
// that to an ordinary one would let the line break in the wrong place.
bool onlyWhitespace(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }
    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
      i += 2;
      continue;
    }
    return false;
  }
  return true;
}

// The kind a block gets, read from the WHOLE STACK rather than from the tag that
// started it -- outermost wins. `<blockquote><p>x</p></blockquote>` is a quoted
// paragraph, not a paragraph that happens to sit inside a quote, and the same for
// `<li><p>`: what the reader should see is decided by the outer element. Reading
// only the innermost tag makes every `<blockquote><p>` a plain paragraph, which is
// how the quote styling silently disappears from a book.
BlockKind kindFromStack(const TagName* stack, size_t depth) {
  for (size_t i = 0; i < depth; ++i) {
    const std::string_view t = stack[i].view();
    if (t == "blockquote") return BlockKind::Blockquote;
    if (t == "li") return BlockKind::ListItem;
    if (t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6")
      return BlockKind::Heading;
  }
  return BlockKind::Paragraph;
}

}  // namespace

// All of the builder's state, in one heap allocation -- the reason Inflater's
// Scratch gives: an Xml is 2,560 bytes and a 64-deep tag stack is another 1,792, so
// a BlockReader as a local would put 4.3 KB in a frame, and this project has
// already had one stack-protection panic from arrays in the wrong place.
struct BlockReader::State {
  Xml xml;
  TagName stack[kMaxNestDepth];
  size_t depth = 0;
  // The depth at which suppression began, 0 for "not suppressing". Nested
  // suppression (a `<style>` inside a `<head>`) needs no counter: the outer one is
  // already in force and the inner close is not at the recorded depth.
  size_t suppressAt = 0;
  Block cur;
  bool open = false;
  bool finished = false;
  // HOW DEEP INSIDE EMPHASIS THE PARSER IS, and a depth rather than a flag because
  // `<em><cite>x</cite></em>` is one emphasised run and not two nested ones. Only
  // the transitions 0->1 and 1->0 open and close a span.
  size_t emDepth = 0;
  // Where the open span started, as an offset into `cur.text`. Meaningful only
  // while emDepth > 0.
  size_t emStart = 0;

  explicit State(ByteSource& src) : xml(src) {}
};

BlockReader::BlockReader(ByteSource& src) : st_(new (std::nothrow) State(src)) {
  if (st_ == nullptr) error_ = "not enough memory to read this chapter";
}

BlockReader::~BlockReader() { delete st_; }

void BlockReader::restart(ByteSource& src) {
  if (st_ == nullptr) return;
  st_->xml.restart(src);
  st_->depth = 0;
  st_->suppressAt = 0;
  st_->cur = Block{};
  st_->open = false;
  st_->finished = false;
  st_->emDepth = 0;
  st_->emStart = 0;
  emitted_ = 0;
  error_ = "";
}

bool BlockReader::next(Block& out) {
  if (st_ == nullptr || !ok() || st_->finished) return false;
  State& st = *st_;

  // Whitespace collapses on the way IN rather than in a pass afterwards, because
  // the alternative is holding a chapter's raw text with every source newline in it
  // and then rewriting it.
  const auto appendSpace = [&]() {
    if (!st.cur.text.empty() && st.cur.text.back() != ' ') st.cur.text.push_back(' ');
  };

  // Closes the emphasis span that is open, if one is, at the text's current end.
  // Returns false only on the cap.
  //
  // AN EMPTY SPAN IS DROPPED. `<em></em>`, and `<em> </em>` once the space collapses
  // away, would otherwise leave a zero-length range that every walk has to skip.
  const auto closeSpan = [&]() -> bool {
    if (st.emStart >= st.cur.text.size()) return true;
    if (st.cur.emphasis.size() >= kMaxEmphasisPerBlock) {
      error_ = "too much emphasis in one block";
      return false;
    }
    st.cur.emphasis.push_back(
        Span{static_cast<uint32_t>(st.emStart),
             static_cast<uint32_t>(st.cur.text.size() - st.emStart)});
    return true;
  };

  // Moves the block being built into `out`, if it has anything in it. An empty
  // block is DROPPED, not emitted blank: `<p></p>` between chapters is a
  // generator's artifact and a blank block would take a line of the page.
  const auto take = [&](bool& have) -> bool {
    have = false;
    if (st.open) {
      // EMPHASIS STILL OPEN AT A BLOCK BOUNDARY IS CLOSED AT IT. `<em><p>a</p>
      // <p>b</p></em>` is markup a book really does contain, and the alternative --
      // carrying the span across the boundary -- would mean a span indexing a
      // string it does not belong to. It reopens against the next block below.
      if (st.emDepth > 0 && !closeSpan()) return false;
      const size_t before = st.cur.text.size();
      while (!st.cur.text.empty() && st.cur.text.back() == ' ') st.cur.text.pop_back();
      // THE TRIM CAN LAND INSIDE A SPAN, and an offset past the end of the string is
      // the kind of thing that reads fine in every test whose text has no trailing
      // space. `<p>a <em>b </em></p>` trims one byte off a span that ended there.
      if (before != st.cur.text.size())
        st.cur.emphasis = clipTo(st.cur.emphasis, 0, st.cur.text.size());
      if (!onlyWhitespace(st.cur.text)) {
        if (emitted_ >= static_cast<int>(kMaxBlocks)) {
          error_ = "too many blocks";
          return false;
        }
        out = std::move(st.cur);
        ++emitted_;
        have = true;
      }
    }
    st.cur = Block{};
    st.open = false;
    return true;
  };

  const auto beginBlock = [&]() {
    st.cur.kind = kindFromStack(st.stack, st.depth);
    st.open = true;
    // Emphasis that was open across the boundary starts again at byte 0 of the new
    // block -- the other half of the rule take() states.
    if (st.emDepth > 0) st.emStart = 0;
  };

  for (;;) {
    const Xml::Node n = st.xml.next();

    if (n == Xml::Node::Error) {
      error_ = st.xml.error();
      return false;
    }

    if (n == Xml::Node::Eof) {
      // UNCLOSED TAGS SURFACE HERE, for free, off the stack this layer needs
      // anyway -- which is why xml.h deliberately keeps no stack of its own.
      if (st.depth != 0) {
        error_ = "unclosed tag at end of document";
        return false;
      }
      st.finished = true;
      bool have = false;
      if (!take(have)) return false;
      return have;
    }

    if (n == Xml::Node::StartTag) {
      if (st.depth >= kMaxNestDepth) {
        error_ = "nesting too deep";
        return false;
      }
      st.stack[st.depth++].set(st.xml.name());

      if (st.suppressAt != 0) continue;
      if (isSuppressed(st.xml.name())) {
        st.suppressAt = st.depth;
        // Whatever block was being built ends at the boundary rather than
        // absorbing the text after the suppressed element.
        bool have = false;
        if (!take(have)) return false;
        if (have) return true;
        continue;
      }
      if (startsBlock(st.xml.name())) {
        // The PREVIOUS block is what goes out; the new one opens against the stack
        // as it stands now, which is why beginBlock runs before the return.
        bool have = false;
        if (!take(have)) return false;
        beginBlock();
        if (have) return true;
        continue;
      }
      if (separatesWords(st.xml.name())) {
        if (!st.open) beginBlock();
        appendSpace();
      }
      if (isEmphasis(st.xml.name())) {
        // A bare `<em>` with no block around it is still the book's words, exactly
        // as a bare text node is -- so it opens one, or `emStart` would index a
        // block that beginBlock is about to replace.
        if (!st.open) beginBlock();
        if (st.emDepth++ == 0) st.emStart = st.cur.text.size();
      }
      continue;
    }

    if (n == Xml::Node::EndTag) {
      if (st.depth == 0) {
        error_ = "end tag with no start tag";
        return false;
      }
      if (!(st.stack[st.depth - 1] == st.xml.name())) {
        error_ = "mismatched end tag";
        return false;
      }
      --st.depth;

      if (st.suppressAt != 0) {
        if (st.depth + 1 == st.suppressAt) st.suppressAt = 0;
        continue;
      }
      if (startsBlock(st.xml.name())) {
        bool have = false;
        if (!take(have)) return false;
        if (have) return true;
        continue;
      }
      if (separatesWords(st.xml.name())) appendSpace();
      if (isEmphasis(st.xml.name()) && st.emDepth > 0) {
        // Only the OUTERMOST close ends the run: `<em><cite>x</cite></em>` is one
        // emphasised phrase. The depth is guarded rather than asserted because an
        // `</em>` with no `<em>` is markup, and this parser reads a user's card --
        // though the stack check above has already refused a mismatched close, so
        // this can only fire on a close whose open was inside a suppressed element.
        if (--st.emDepth == 0 && !closeSpan()) return false;
      }
      continue;
    }

    // Text.
    if (st.suppressAt != 0) continue;
    // A bare text node with no block around it is still the book's words, so it
    // opens one rather than being lost.
    if (!st.open) beginBlock();
    for (const char c : st.xml.text()) {
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        appendSpace();
        continue;
      }
      if (st.cur.text.size() >= kMaxBlockBytes) {
        error_ = "block too long";
        return false;
      }
      st.cur.text.push_back(c);
    }
  }
}

bool buildDocument(std::string_view xhtml, Document& out, const char** reason) {
  out.blocks.clear();
  BufferSource src(xhtml);
  BlockReader r(src);
  if (!r.ok()) {
    *reason = r.error();
    return false;
  }
  Block b;
  while (r.next(b)) out.blocks.push_back(std::move(b));
  if (!r.ok()) {
    *reason = r.error();
    out.blocks.clear();
    return false;
  }
  return true;
}

}  // namespace reader
